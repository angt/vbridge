#pragma once

enum command {
    command_start,
    command_next,
    command_auth_ssl,
    command_auth_pam,
    command_auth_gss,
    command_control,
    command_pointer,
    command_pointer_sync,
    command_button,
    command_key,
    command_quality,
    command_resize,
    command_access,
    command_master,
    command_clipboard,
    command_cursor,
    command_cursor_data,
    command_image,
    command_image_data,
    command_stop,
};

typedef enum command command_t;
