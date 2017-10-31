#include "image.h"
#include "common-static.h"

#include <sys/ipc.h>
#include <sys/shm.h>

void
image_create(image_t *image, Drawable drawable, int w, int h)
{
    if (!image)
        return;

    image->gc = XCreateGC(display.id, drawable, 0, NULL);
    image->drawable = drawable;

    if (!XShmQueryExtension(display.id))
        goto no_shm;

    image->id = XShmCreateImage(display.id, display.visual, display.depth,
                                ZPixmap, NULL, &image->shm, w, h);
    if (!image->id)
        goto no_shm;

    image->shm.shmid = shmget(IPC_PRIVATE, h * image->id->bytes_per_line,
                              IPC_CREAT | 0600);

    if (image->shm.shmid == -1)
        goto destroy;

    image->shm.shmaddr = shmat(image->shm.shmid, NULL, 0);

    if (image->shm.shmaddr == (void *)-1)
        goto destroy;

    image->shm.readOnly = False;

    if (!XShmAttach(display.id, &image->shm))
        goto detach;

    image->id->data = image->shm.shmaddr;

    goto end;

detach:
    shmdt(image->shm.shmaddr);

destroy:
    XDestroyImage(image->id);

no_shm:
    image->shm.shmid = -1;
    warning("couldn't create XShm image\n");

    image->id = XCreateImage(display.id, display.visual, display.depth,
                             ZPixmap, 0, NULL, w, h, 32, 0);

    if (!image->id)
        error("couldn't create image\n");

    image->id->data = safe_malloc(h * image->id->bytes_per_line);

end:
    if (image->id->bytes_per_line < 4 * w)
        error("image's pixels must be stored in 4 bytes\n");

    if (image->id->bytes_per_line % 4)
        error("image's bytes per line must be a multiple of 4\n");

    byte_set(image->id->data, 0, h * image->id->bytes_per_line);

    image->info.data = (uint32_t *)image->id->data;
    image->info.stride = image->id->bytes_per_line / 4;
    image->info.w = w;
    image->info.h = h;
}

void
image_delete(image_t *image)
{
    if (!image || !image->id)
        return;

    XDestroyImage(image->id);

    if (image->shm.shmid != -1) {
        XShmDetach(display.id, &image->shm);
        shmdt(image->shm.shmaddr);
        shmctl(image->shm.shmid, IPC_RMID, NULL);
    }

    if (image->gc)
        XFreeGC(display.id, image->gc);

    byte_set(image, 0, sizeof(image_t));
    image->shm.shmid = -1;
}

void
image_get(image_t *image, int draw_x, int draw_y)
{
    if (!image)
        return;

    if (image->shm.shmid != -1) {
        XShmGetImage(display.id, image->drawable, image->id, draw_x, draw_y,
                     AllPlanes);
    } else {
        XGetSubImage(display.id, image->drawable, draw_x, draw_y,
                     image->id->width, image->id->height, AllPlanes,
                     ZPixmap, image->id, 0, 0);
    }
}

void
image_put(image_t *image, int x, int y,
          int draw_x, int draw_y, int draw_w, int draw_h)
{
    if (!image)
        return;

    if (image->shm.shmid != -1) {
        XShmPutImage(display.id, image->drawable, image->gc, image->id,
                     x, y, draw_x, draw_y, draw_w, draw_h, False);
    } else {
        XPutImage(display.id, image->drawable, image->gc, image->id,
                  x, y, draw_x, draw_y, draw_w, draw_h);
    }
}
