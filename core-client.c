#include "core-client.h"
#include "buffer-static.h"

struct buffer_list {
    buffer_t buffer;
    buffer_list_t *next;
};

struct cursor {
    uint32_t hash;
    buffer_t recv;
};

int
core_create(core_client_t *core, const char *host, const char *port)
{
    byte_set(core, 0, sizeof(core_client_t));

    if (netio_create(&core->netio, host, port, 0) < 0)
        return 0;

    core->cursor.cache = safe_calloc(256, sizeof(cursor_t));

    return 1;
}

void
core_delete(core_client_t *core)
{
    tycho_delete(&core->tycho);
    netio_delete(&core->netio);

    if (core->cursor.cache) {
        for (int i = 0; i < 256; i++)
            safe_free(core->cursor.cache[i].recv.data);
    }

    safe_free(core->cursor.cache);

    byte_set(core, 0, sizeof(core_client_t));
}

int
core_send_all(core_client_t *core)
{
    netio_t *const netio = &core->netio;
    buffer_t *const output = &netio->output;

    buffer_list_t **bs = &core->buffers;

    while (*bs) {
        buffer_t *buffer = &(*bs)->buffer;
        buffer_copy(output, buffer);
        if (buffer_read_size(buffer))
            break;
        buffer_list_t *tmp = *bs;
        *bs = tmp->next;
        safe_free(tmp);
    }

    if (time_diff(&core->send.time, core->send.timeout) &&
        !netio_write(netio))
        return 0;

    return 1;
}

int
core_recv_all(core_client_t *core)
{
    netio_t *const netio = &core->netio;
    buffer_t *const input = &netio->input;

    core->recv.mask = 0;

    while (1) {
        switch (core->recv.command) {

        case command_start:
            {
                switch (netio_start(netio)) {
                case 0:  return 0;
                case -1: return 1;
                }

                break;
            }

        case command_next:
            {
                if (buffer_read_size(input) < 1)
                    goto read_again;

                core->recv.command = buffer_read(input);

                continue;
            }

        case command_auth_gss:
            {
                buffer_t *buffer = &core->gss.recv;

                if (buffer->data && !buffer_write_size(buffer))
                    return 1;

                if (token_recv(buffer, input))
                    goto read_again;

                break;
            }

        case command_access: // rename auth
            {
                if (buffer_read_size(input) < 2)
                    goto read_again;

                core->level = buffer_read(input);
                core->access = buffer_read(input);

                break;
            }

        case command_master:
            {
                if (buffer_read_size(input) < 1)
                    goto read_again;

                core->master = buffer_read(input);

                break;
            }

        case command_pointer:
            {
                if (buffer_read_size(input) < 4)
                    goto read_again;

                core->pointer.x = (int16_t)buffer_read_16(input);
                core->pointer.y = (int16_t)buffer_read_16(input);

                break;
            }

        case command_pointer_sync:
            {
                if (buffer_read_size(input) < 4)
                    goto read_again;

                if (core->pointer.sx || core->pointer.sy)
                    return 1;

                core->pointer.sx = (int16_t)buffer_read_16(input);
                core->pointer.sy = (int16_t)buffer_read_16(input);

                break;
            }

        case command_cursor:
            {
                if (buffer_read_size(input) < 12)
                    goto read_again;

                uint32_t hash = buffer_read_32(input);

                core->cursor.w = buffer_read_16(input);
                core->cursor.h = buffer_read_16(input);

                core->cursor.x = (int16_t)buffer_read_16(input);
                core->cursor.y = (int16_t)buffer_read_16(input);

                int i = 0;

                for (; i < 256; i++) {
                    if (hash == core->cursor.cache[i].hash)
                        break;
                }

                core->cursor.serial = i;

                if ((!hash) || (i < 256) ||
                        (core->cursor.w <= 0) ||
                        (core->cursor.h <= 0))
                    break;

                core->cursor.serial = 0;

                safe_free(core->cursor.cache[255].recv.data);

                for (int k = 255; k; k--)
                    core->cursor.cache[k] = core->cursor.cache[k - 1];

                core->cursor.cache[0].hash = hash;

                buffer_setup(&core->cursor.cache[0].recv,
                        NULL, core->cursor.w * core->cursor.h * 4);

                core->recv.command++;
            }

        case command_cursor_data:
            {
                buffer_t *buffer = &core->cursor.cache[0].recv;

                buffer_copy(buffer, input);

                if (buffer_write_size(buffer))
                    goto read_again;

                break;
            }

        case command_image:
            {
                if (buffer_read_size(input) < 4)
                    goto read_again;

                core->size.w = buffer_read_16(input);
                core->size.h = buffer_read_16(input);

                if (core->size.w <= 0 || core->size.h <= 0) {
                    core->recv.command = command_stop; // XXX
                    continue;
                }

                tycho_setup(&core->tycho, core->size.w, core->size.h);

                core->recv.command++;
            }

        case command_image_data:
            {
                if (core->size.w != core->image.w ||
                        core->size.h != core->image.h || !core->image.data) {
                    return 1;
                }

                if (tycho_recv(&core->tycho, input, &core->image))
                    goto read_again;

                core->send.timeout = 30;
                core->send.time = 0;

                core_send(core, command_image);

                break;
            }

        case command_control:
            {
                buffer_t *buffer = &core->control.recv;

                if (buffer->data && !buffer_write_size(buffer))
                    return 1;

                if (token_recv(buffer, input))
                    goto read_again;

                break;
            }

        case command_clipboard:
            {
                buffer_t *buffer = &core->clipboard.recv;

                if (buffer->data && !buffer_write_size(buffer))
                    return 1;

                if (token_recv(buffer, input))
                    goto read_again;

                break;
            }

        default:
        case command_stop:
            {
                if (netio_stop(netio) == -1)
                    return 1;

                warning("%s: unknown command from server\n", netio->name); // XXX
                return 0;
            }
        }

        core->recv.mask |= (1 << core->recv.command);
        core->recv.command = command_next;
        continue;

    read_again:
        switch (netio_read(netio)) {
        case 0:  return 0;
        case -1: return 1;
        }
    }

    return 0;
}

static buffer_t *
get_buffer(core_client_t *core, size_t size)
{
    buffer_t *buffer = &core->netio.output;

    if (!core->buffers && buffer_write_size(buffer) >= size)
        return buffer;

    buffer_list_t **bs = &core->buffers; // use last ptr

    while (*bs)
        bs = &(*bs)->next;

    *bs = safe_malloc(sizeof(buffer_list_t) + size);
    (*bs)->next = NULL;

    buffer = &(*bs)->buffer;
    buffer_setup(buffer, (uint8_t *)(*bs) + sizeof(buffer_list_t), size);

    return buffer;
}

void
core_send(core_client_t *core, command_t command)
{
    buffer_t *const buffer = get_buffer(core, 1);

    buffer_write(buffer, command);

    core_send_all(core);
}

void
core_send_quality(core_client_t *core, unsigned min, unsigned max)
{
    if (!core->access)
        return;

    buffer_t *const buffer = get_buffer(core, 3);

    buffer_write(buffer, command_quality);
    buffer_write(buffer, min);
    buffer_write(buffer, max);

    core_send_all(core);
}

void
core_send_resize(core_client_t *core, unsigned w, unsigned h)
{
    if (!core->access)
        return;

    buffer_t *const buffer = get_buffer(core, 5);

    buffer_write(buffer, command_resize);
    buffer_write_16(buffer, w);
    buffer_write_16(buffer, h);

    core_send_all(core);
}

void
core_send_pointer(core_client_t *core, int x, int y, int sync)
{
    if (!core->access)
        return;

    buffer_t *const buffer = get_buffer(core, 5);

    command_t command = command_pointer;

    const int16_t px = x;
    const int16_t py = y;

    if (sync) {
        command = command_pointer_sync;
        x -= core->pointer.px;
        y -= core->pointer.py;
        core->pointer.sx = 0;
        core->pointer.sy = 0;
    }

    core->pointer.px = px;
    core->pointer.py = py;

    buffer_write(buffer, command);
    buffer_write_16(buffer, x);
    buffer_write_16(buffer, y);

    core_send_all(core);
}

void
core_send_button(core_client_t *core, unsigned button, int press)
{
    if (!core->access)
        return;

    buffer_t *const buffer = get_buffer(core, 3);

    buffer_write(buffer, command_button);
    buffer_write(buffer, button);
    buffer_write(buffer, press);

    core_send_all(core);
}

void
core_send_key(core_client_t *core, int ucs, unsigned symbol, int key, int press)
{
    if (!core->access)
        return;

    buffer_t *const buffer = get_buffer(core, 8);

    buffer_write(buffer, command_key);
    buffer_write(buffer, ucs);
    buffer_write_32(buffer, symbol);
    buffer_write(buffer, key);
    buffer_write(buffer, press);

    core_send_all(core);
}

void
core_send_data(core_client_t *core, command_t command, const void *data, size_t size)
{
    if (!core->access)
        return;

    if (!data || !size)
        return;

    buffer_t *const buffer = get_buffer(core, 5 + size);

    buffer_write(buffer, command);
    buffer_write_32(buffer, size);
    buffer_write_data(buffer, data, size);

    core_send_all(core);
}

void
core_send_auth(core_client_t *core, const char *name, const char *pass)
{
    size_t name_size = str_len(name);
    size_t pass_size = str_len(pass);

    if (!name_size || !pass_size)
        return;

    buffer_t *const buffer = get_buffer(core, 9 + name_size + pass_size);

    buffer_write(buffer, command_auth_pam);

    buffer_write_32(buffer, name_size);
    buffer_write_data(buffer, name, name_size);

    buffer_write_32(buffer, pass_size);
    buffer_write_data(buffer, pass, pass_size);

    core_send_all(core);
}

uint32_t *
core_recv_cursor(core_client_t *core)
{
    if (!(core->recv.mask & (1 << command_cursor)))
        return NULL;

    size_t size = core->cursor.w * core->cursor.h * 4;

    if (core->cursor.serial >= 256)
        return safe_calloc(1, size);

    buffer_t *buffer = &core->cursor.cache[core->cursor.serial].recv;

    if (buffer_write_size(buffer))
        return NULL;

    uint32_t *data = NULL;

    if (size && (size == buffer_read_size(buffer))) {
        data = safe_malloc(size);
        for (size_t k = 0; k < (size >> 2); k++)
            data[k] = buffer_read_32(buffer);
        buffer->read = buffer->data;
    }

    return data;
}

void *
core_recv_control(core_client_t *core)
{
    if (buffer_write_size(&core->control.recv))
        return NULL;

    void *data = core->control.recv.data;
    byte_set(&core->control.recv, 0, sizeof(buffer_t));

    return data;
}

void *
core_recv_clipboard(core_client_t *core)
{
    if (buffer_write_size(&core->clipboard.recv))
        return NULL;

    void *data = core->clipboard.recv.data;
    byte_set(&core->clipboard.recv, 0, sizeof(buffer_t));

    return data;
}

int
core_received(core_client_t *core, command_t command)
{
    return core->recv.mask & (1 << command);
}
