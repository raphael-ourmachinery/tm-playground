extern "C" {
#include "metal_adder.h"
#include "loader.h"

#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/buffer_format.h>
#include <foundation/carray_print.inl>
#include <foundation/hash.inl>
#include <foundation/job_system.h>
#include <foundation/log.h>
#include <foundation/math.inl>
#include <foundation/murmurhash64a.inl>
#include <foundation/os.h>
#include <foundation/profiler.h>
#include <foundation/sort.inl>

#include <plugins/os_window/os_window.h>
}

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>

// The number of floats in each array, and the size of the arrays in bytes.
const uint32_t array_length = 1 << 24;
const uint32_t buffer_size = array_length * sizeof(float);

struct metal_adder_o {
    tm_allocator_i allocator;

    MTL::Device *device;
    MTL::ComputePipelineState *pipeline;
    MTL::CommandQueue *command_queue;

    MTL::Buffer *buffer_a;
    MTL::Buffer *buffer_b;
    MTL::Buffer *result;

    MTL::Function *adder;
};

static void private__generate_random_float_data(MTL::Buffer *buffer)
{
    float *data = (float *)buffer->contents();

    for (uint32_t i = 0; i < array_length; ++i) {
        data[i] = (float)rand()/(float)(RAND_MAX);
    }
}

static struct metal_adder_o *init(struct tm_allocator_i *allocator, const char *data_dir)
{
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

    TM_INIT_TEMP_ALLOCATOR(ta);

    tm_allocator_i a = tm_allocator_api->create_child(allocator, "metal_adder");
    metal_adder_o *m = (metal_adder_o *)tm_alloc(&a, sizeof(metal_adder_o));
    memset(m, 0, sizeof(metal_adder_o));
    m->allocator = a;

    // Init device
    m->device = MTL::CreateSystemDefaultDevice();

    // Load shader file
    NS::Error *error = NULL;
    const char *shader_path = tm_temp_allocator_api->printf(ta, "%sshaders/metal_adder.metal", data_dir);
    tm_file_o shader = tm_os_api->file_io->open_input(shader_path);
    uint64_t size = tm_os_api->file_io->size(shader);
    char *shader_source = (char *)tm_temp_alloc(ta, size);
    tm_os_api->file_io->read(shader, shader_source, size);
    tm_os_api->file_io->close(shader);

    NS::String *source = NS::String::string(shader_source, NS::ASCIIStringEncoding);
    MTL::CompileOptions *options = MTL::CompileOptions::alloc()->init();
    MTL::Library *library = m->device->newLibrary(source, options, &error);
    if (error) {
        TM_LOG("Error in shader library creation: %s\n", error->localizedDescription()->cString(NS::UTF8StringEncoding));
        return NULL;
    }
    m->adder = library->newFunction(NS::String::string("add_arrays", NS::ASCIIStringEncoding));

    options->release();
    library->release();

    // Create compute pipeline state
    m->pipeline = m->device->newComputePipelineState(m->adder, &error);
    if (!m->pipeline) {
        TM_LOG("Failed to create compute pipeline state: %s\n", error->localizedDescription()->cString(NS::UTF8StringEncoding));
        return NULL;
    }

    m->command_queue = m->device->newCommandQueue();

    // Create and prepare data
    m->buffer_a = m->device->newBuffer(buffer_size, MTL::ResourceStorageModeShared);
    m->buffer_b = m->device->newBuffer(buffer_size, MTL::ResourceStorageModeShared);
    m->result = m->device->newBuffer(buffer_size, MTL::ResourceStorageModeShared);
    private__generate_random_float_data(m->buffer_a);
    private__generate_random_float_data(m->buffer_b);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);


    pool->release();

    return m;
}

static void private__verify_results(metal_adder_o *metal_adder)
{
    float* a = (float *)metal_adder->buffer_a->contents();
    float* b = (float *)metal_adder->buffer_b->contents();
    float* result = (float *)metal_adder->result->contents();

    for (uint64_t i = 0; i < array_length; i++)
    {
        if (result[i] != (a[i] + b[i])) {
            TM_LOG("Compute ERROR: index=%lu result=%g vs %g=a+b\n",
                   i, result[i], a[i] + b[i]);
            assert(result[i] == (a[i] + b[i]));
        }
    }
    TM_LOG("Compute results as expected\n");
}

static void send_compute_command(struct metal_adder_o *metal_adder)
{
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();
    //Create command buffer to hold commands
    MTL::CommandBuffer *command_buffer = metal_adder->command_queue->commandBuffer();

    // Start a compute pass.
    MTL::ComputeCommandEncoder *compute_encoder = command_buffer->computeCommandEncoder();
    compute_encoder->setComputePipelineState(metal_adder->pipeline);
    compute_encoder->setBuffer(metal_adder->buffer_a, 0, 0);
    compute_encoder->setBuffer(metal_adder->buffer_b, 0, 1);
    compute_encoder->setBuffer(metal_adder->result, 0, 2);

    MTL::Size grid_size = MTL::Size::Make(array_length, 1, 1);

    NS::UInteger thread_group_size = metal_adder->pipeline->maxTotalThreadsPerThreadgroup();
    thread_group_size = thread_group_size > array_length ? array_length : thread_group_size;

    MTL::Size group_size = MTL::Size::Make(thread_group_size, 1, 1);

    compute_encoder->dispatchThreads(grid_size, group_size);
    compute_encoder->endEncoding();

    command_buffer->commit();
    command_buffer->waitUntilCompleted();

    private__verify_results(metal_adder);

    pool->release();

}

static void shutdown(struct metal_adder_o *metal_adder)
{
    metal_adder->pipeline->release();
    metal_adder->command_queue->release();
    metal_adder->buffer_a->release();
    metal_adder->buffer_b->release();
    metal_adder->result->release();
    metal_adder->adder->release();
    
    tm_allocator_i a = metal_adder->allocator;
    tm_free(&a, metal_adder, sizeof(metal_adder_o));
    tm_allocator_api->destroy_child(&a);
}

static struct metal_adder_api adder_api = {
    .init = init,
    .send_compute_command = send_compute_command,
    .shutdown = shutdown,
};

extern "C" {
void load_metal_adder(struct tm_api_registry_api *reg, bool load);
}

void load_metal_adder(struct tm_api_registry_api *reg, bool load)
{
    tm_set_or_remove_api(reg, load, metal_adder_api, &adder_api);
}
