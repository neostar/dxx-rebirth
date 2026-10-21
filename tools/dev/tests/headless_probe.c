/*
 * This is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/* Minimal headless SDL3 GPU tool used by no_window_test.sh: it initialises
 * video and a GPU device the way the fork's headless tools do, without a
 * window, and stays alive briefly so the frontmost app can be sampled.
 */
#include <SDL3/SDL.h>
#include <stdio.h>

int main(void)
{
	/* Video init foregrounds the process on macOS unless this is set. */
	SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "headless_probe: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	/* debug_mode true, as ADR 5 asks of every SDL_GPU device. */
	SDL_GPUDevice *dev =
	    SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
	if (!dev)
	{
		fprintf(stderr, "headless_probe: SDL_CreateGPUDevice: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	printf("headless_probe: gpu device %s\n", SDL_GetGPUDeviceDriver(dev));
	fflush(stdout);
	SDL_Delay(1500);
	SDL_DestroyGPUDevice(dev);
	SDL_Quit();
	return 0;
}
