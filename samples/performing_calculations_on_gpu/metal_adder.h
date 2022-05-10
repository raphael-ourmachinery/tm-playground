#pragma once

struct metal_adder_o;
struct tm_allocator_i;

struct metal_adder_api {
    struct metal_adder_o *(*init)(struct tm_allocator_i *allocator, const char *data_dir);
    void (*send_compute_command)(struct metal_adder_o *metal_adder);
    void (*shutdown)(struct metal_adder_o *metal_adder);
};

#define metal_adder_api_version TM_VERSION(1, 0, 0)
