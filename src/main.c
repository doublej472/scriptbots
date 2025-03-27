
// SDL3
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "AVXBrain.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Global Vars:

const int NUM_THREADS = 4;

struct AppState {
	struct SDL_Window* window;
	struct SDL_Thread** threads;
	struct SDL_GPUDevice* device;
	uint64_t frames;
};

// struct Base base;


// ---------------------------------------------------------------------------
// Prototypes:

// void signal_handler(int signum) { base.world->stopSim = 1; }


SDL_GPUShader* LoadVulkanShader(
	SDL_GPUDevice* device,
	const char* shaderFilename,
	Uint32 samplerCount,
	Uint32 uniformBufferCount,
	Uint32 storageBufferCount,
	Uint32 storageTextureCount
) {

	// Auto-detect the shader stage from the file name for convenience
	SDL_GPUShaderStage stage;
	if (SDL_strstr(shaderFilename, ".vert"))
	{
		stage = SDL_GPU_SHADERSTAGE_VERTEX;
	}
	else if (SDL_strstr(shaderFilename, ".frag"))
	{
		stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	}
	else
	{
		SDL_Log("Invalid shader stage!");
		return NULL;
	}

	char fullPath[2048];
	SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
	const char* entrypoint;


	SDL_snprintf(fullPath, sizeof(fullPath), "%sContent/Shaders/Compiled/SPIRV/%s.spv", SDL_GetBasePath(), shaderFilename);
	format = SDL_GPU_SHADERFORMAT_SPIRV;
	entrypoint = "main";


	size_t codeSize;
	void* code = SDL_LoadFile(fullPath, &codeSize);
	if (code == NULL)
	{
		SDL_Log("Failed to load shader from disk! %s", fullPath);
		return NULL;
	}

	SDL_GPUShaderCreateInfo shaderInfo = {
		.code = code,
		.code_size = codeSize,
		.entrypoint = entrypoint,
		.format = format,
		.stage = stage,
		.num_samplers = samplerCount,
		.num_uniform_buffers = uniformBufferCount,
		.num_storage_buffers = storageBufferCount,
		.num_storage_textures = storageTextureCount
	};
	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
	if (shader == NULL)
	{
		SDL_Log("Failed to create shader!");
		SDL_free(code);
		return NULL;
	}

	SDL_free(code);
	return shader;
}

int worker_thread(void* arg) {
	// struct Queue *queue = (struct Queue *)arg;

	// while (1) {
	//   struct QueueItem qi = queue_dequeue(queue);
	//   assert(qi.data != NULL);
	//   assert(qi.function != NULL);
	//   qi.function(qi.data);
	//   queue_workdone(queue);
	// }

	return 0;
}



static void print_sdl_info() {
	const int compiled = SDL_VERSION;  /* hardcoded number from SDL headers */
	const int linked = SDL_GetVersion();  /* reported by linked SDL library */

	SDL_Log("We compiled against SDL version %d.%d.%d ...\n",
		SDL_VERSIONNUM_MAJOR(compiled),
		SDL_VERSIONNUM_MINOR(compiled),
		SDL_VERSIONNUM_MICRO(compiled));

	SDL_Log("But we are linking against SDL version %d.%d.%d.\n",
		SDL_VERSIONNUM_MAJOR(linked),
		SDL_VERSIONNUM_MINOR(linked),
		SDL_VERSIONNUM_MICRO(linked));

	SDL_Log("SDL Revision: %s\n", SDL_GetRevision());
}

static int init_sdl(struct AppState* aState) {
	SDL_Log("Initializing SDL\n");
	int ret = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

	if (!ret) {
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL!\n");
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s\n", SDL_GetError());
		return ret;
	}

	aState->device = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV,
		false,
		NULL);

	if (aState->device == NULL)
	{
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GPUCreateDevice failed!\n");
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s\n", SDL_GetError());
		return -1;
	}

	aState->window = SDL_CreateWindow("Scriptbots", 800, 600, SDL_WINDOW_RESIZABLE);
	if (aState->window == NULL) {
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window!\n");
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s\n", SDL_GetError());
		return -1;
	}

	if (!SDL_ClaimWindowForGPUDevice(aState->device, aState->window))
	{
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GPUClaimWindow failed!\n");
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s\n", SDL_GetError());
		return -1;
	}

	return 0;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
	SDL_Log("App Start");

	struct AppState* aState = SDL_malloc(sizeof(struct AppState));
	*appstate = aState;
	aState->frames = 0;

	print_sdl_info();
	init_sdl(aState);

	aState->threads = SDL_malloc(sizeof(struct SDL_Thread*) * NUM_THREADS);

	//for (int i = 0; i < NUM_THREADS; i++) {
	//	SDL_Log("Creating thread\n");
	//	aState->threads[i] = SDL_CreateThread(worker_thread, "worker", NULL);
	//}

	// // Load file if needed
	// if (loadWorldFromFile) {
	//   base_loadworld(&base);

	//   // check if epoch is greater than passed parameter
	//   if (base.world->current_epoch > MAX_EPOCHS)
	//     printf("\nWarning: the loaded file has an epoch later than the specefied "
	//            "end time parameter\n");
	// }

	struct AVXBrain *b = SDL_aligned_alloc(64, sizeof(struct AVXBrain));

	avxbrain_init_random(b);
	avxbrain_print(b);

	float inputs[BRAIN_INPUT_SIZE];
	for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
		inputs[i] = SDL_randf();
	}

	float outputs[BRAIN_OUTPUT_SIZE];
	for (int i = 0; i < BRAIN_OUTPUT_SIZE; i++) {
		outputs[i] = 0.0f;
	}

	avxbrain_tick(b, &inputs, &outputs);

	for (int i = 0; i < BRAIN_INPUT_SIZE; i++) {
		SDL_Log("inputs[%d] = %f\n", i, inputs[i]);
	}
	for (int i = 0; i < BRAIN_OUTPUT_SIZE; i++) {
		SDL_Log("outputs[%d] = %f\n", i, outputs[i]);
	}

	//avxbrain_print(b);

	SDL_aligned_free(b);

	return SDL_APP_CONTINUE;
};

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
	struct AppState* aState = (struct AppState*)appstate;
	// base_saveworld(&base);
	// queue_close(base.world->queue);

	// for (int i = 0; i < base.world->agents.size; i++) {
	//   free_brain(base.world->agents.agents[i]->brain);
	// }
	// for (int i = 0; i < base.world->agents_staging.size; i++) {
	//   free_brain(base.world->agents_staging.agents[i]->brain);
	// }
	// avec_free(&base.world->agents);
	// avec_free(&base.world->agents_staging);

	for (int i = 0; i < NUM_THREADS; i++) {
		SDL_WaitThread(aState->threads[i], NULL);
	}

	SDL_ReleaseWindowFromGPUDevice(aState->device, aState->window);
	SDL_DestroyWindow(aState->window);
	SDL_DestroyGPUDevice(aState->device);


	// free(base.world->queue);
	// free(base.world);
	SDL_free(aState->threads);
	SDL_free(aState);
}

// ---------------------------------------------------------------------------
// Run Scriptbots
// ---------------------------------------------------------------------------
SDL_AppResult SDL_AppIterate(void* appstate) {
	struct AppState* aState = (struct AppState*) appstate;
	aState->frames++;

	// TICK

	// DRAW

	SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(aState->device);
	if (cmdbuf == NULL)
	{
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "AcquireGPUCommandBuffer failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_GPUTexture* swapchainTexture;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, aState->window, &swapchainTexture, NULL, NULL)) {
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	if (swapchainTexture != NULL)
	{
		SDL_GPUColorTargetInfo colorTargetInfo = { 0 };
		colorTargetInfo.texture = swapchainTexture;
		colorTargetInfo.clear_color = (SDL_FColor){ 1.0f , 0.0f, 0.0f, 1.0f };
		colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
		colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdbuf, &colorTargetInfo, 1, NULL);
		SDL_EndGPURenderPass(renderPass);
	}

	SDL_SubmitGPUCommandBuffer(cmdbuf);

	// world_update(base.world);
	return SDL_APP_CONTINUE;
}


SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	struct AppState* aState = (struct AppState*)appstate;

	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Got event %d.\n", event->type);

	switch (event->type) {
	case SDL_EVENT_QUIT:
		return SDL_APP_SUCCESS;
	default:
		break;
	}

	return SDL_APP_CONTINUE;
}