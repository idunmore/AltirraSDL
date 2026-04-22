//	AltirraSDL - Online Play emote icons.
//	Decode 16 baked PNG blobs into ImGui textures on first use.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <cstring>

#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include "emotes_data.h"
#include "display_backend.h"
#include "gl_helpers.h"

#include "emote_assets.h"

extern SDL_Window *g_pWindow;
extern IDisplayBackend *ATUIGetDisplayBackend();

namespace ATEmotes {

namespace {

struct EmoteTexture {
	ImTextureID mTexID = (ImTextureID)0;
	int mWidth = 0;
	int mHeight = 0;
	GLuint mGLTexture = 0;
	SDL_Texture *mSDLTexture = nullptr;
};

bool gInitialized = false;
bool gReady = false;
EmoteTexture gTextures[kCount];

SDL_Surface *DecodePNG(const uint8_t *data, size_t size) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	SDL_IOStream *io = SDL_IOFromConstMem(data, size);
	if (!io)
		return nullptr;

	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		return nullptr;

	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	SDL_DestroySurface(surf);
	return conv;
#else
	(void)data; (void)size;
	return nullptr;
#endif
}

bool UploadTexture(EmoteTexture &out, SDL_Surface *surf) {
	if (!surf || surf->w <= 0 || surf->h <= 0)
		return false;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;

	out.mWidth = surf->w;
	out.mHeight = surf->h;

	if (useGL) {
		out.mGLTexture = GLCreateXRGB8888Texture(surf->w, surf->h, true, nullptr);
		if (!out.mGLTexture)
			return false;

		std::vector<uint32_t> buf((size_t)surf->w * (size_t)surf->h);
		const uint8_t *src = (const uint8_t *)surf->pixels;
		for (int y = 0; y < surf->h; ++y)
			memcpy(&buf[(size_t)y * (size_t)surf->w],
				src + (size_t)y * (size_t)surf->pitch,
				(size_t)surf->w * 4);

		glBindTexture(GL_TEXTURE_2D, out.mGLTexture);
		GLUploadXRGB8888(surf->w, surf->h, buf.data(), 0);

		out.mTexID = (ImTextureID)(intptr_t)out.mGLTexture;
		return true;
	}

	SDL_Renderer *renderer = g_pWindow ? SDL_GetRenderer(g_pWindow) : nullptr;
	if (!renderer)
		return false;

	out.mSDLTexture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_BGRA32,
		SDL_TEXTUREACCESS_STREAMING, surf->w, surf->h);
	if (!out.mSDLTexture)
		return false;

	void *pixels = nullptr;
	int pitch = 0;
	if (!SDL_LockTexture(out.mSDLTexture, nullptr, &pixels, &pitch)) {
		SDL_DestroyTexture(out.mSDLTexture);
		out.mSDLTexture = nullptr;
		return false;
	}

	const uint8_t *src = (const uint8_t *)surf->pixels;
	uint8_t *dst = (uint8_t *)pixels;
	int copyBytes = surf->w * 4;
	for (int y = 0; y < surf->h; ++y) {
		memcpy(dst, src, (size_t)copyBytes);
		src += surf->pitch;
		dst += pitch;
	}
	SDL_UnlockTexture(out.mSDLTexture);

	out.mTexID = (ImTextureID)out.mSDLTexture;
	return true;
}

void DestroyTexture(EmoteTexture &tex) {
	if (tex.mGLTexture) {
		glDeleteTextures(1, &tex.mGLTexture);
		tex.mGLTexture = 0;
	}
	if (tex.mSDLTexture) {
		SDL_DestroyTexture(tex.mSDLTexture);
		tex.mSDLTexture = nullptr;
	}
	tex.mTexID = (ImTextureID)0;
	tex.mWidth = 0;
	tex.mHeight = 0;
}

} // namespace

void Initialize() {
	if (gInitialized)
		return;
	gInitialized = true;

	int loaded = 0;
	for (int i = 0; i < kCount; ++i) {
		const ATEmoteBlob &blob = kATEmoteBlobs[i];
		SDL_Surface *surf = DecodePNG(blob.data, blob.size);
		if (!surf) {
			SDL_Log("Emotes: failed to decode icon%02d", i + 1);
			continue;
		}

		bool ok = UploadTexture(gTextures[i], surf);
		SDL_DestroySurface(surf);
		if (ok)
			++loaded;
		else
			SDL_Log("Emotes: failed to upload icon%02d", i + 1);
	}

	gReady = (loaded == kCount);
	if (!gReady)
		SDL_Log("Emotes: only %d/%d textures available", loaded, kCount);
}

void Shutdown() {
	for (auto &t : gTextures)
		DestroyTexture(t);
	gReady = false;
	gInitialized = false;
}

bool IsReady() {
	return gReady;
}

ImTextureID GetTexture(int iconId, int *outW, int *outH) {
	if (!gReady || iconId < 0 || iconId >= kCount)
		return (ImTextureID)0;
	if (outW) *outW = gTextures[iconId].mWidth;
	if (outH) *outH = gTextures[iconId].mHeight;
	return gTextures[iconId].mTexID;
}

} // namespace ATEmotes
