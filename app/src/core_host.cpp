// lol-vr — hôte du cœur ScummVM (interface libretro). Voir core_host.h.
#include "core_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "font8x16.h"
#include "libretro.h"

// État partagé exporté par le cœur (voir patches/0002 et engines/kyra/engine/lol.h).
extern "C" {
struct LolvrGameState {
	int32_t version;
	int32_t level;
	char tileset[16];
	uint32_t sfxSerial;
	int32_t sfxTrack;
	int32_t sfxVolume;
	uint32_t hurtSerial;
	int32_t hurtDamage;
	uint32_t hitSerial;
	int32_t hitDamage;
	char sfxName[16];
	int32_t reserved[4];
};
const LolvrGameState *retro_lolvr_state(void);
void retro_lolvr_set_lang(int lang);
void retro_lolvr_inventory(int op, int arg);
void retro_lolvr_hide_cursor(int hide);
int retro_lolvr_cursor_shown(void);

// Un monstre ou un objet près du groupe (voir patches/0003).
struct LolvrSprite {
	int16_t x, y;
	uint8_t kind;
	uint8_t ceiling;
	int16_t x0, y0, x1, y1;
	uint32_t version;
	uint16_t id;
	uint8_t flying;
	uint8_t flightDir;
};

struct LolvrFlight {
	uint16_t item;
	uint16_t x, y;
	uint8_t height;
	uint8_t dir;
};

// Le niveau en cours, pour le donjon en 3D (voir patches/0003).
struct LolvrLevelData {
	int32_t version;
	uint32_t gfxSerial;
	uint32_t wallsSerial;
	int32_t level;
	int32_t block;
	int32_t direction;
	int32_t inDungeon;
	uint8_t walls[1024 * 4];
	const uint8_t *wllVmpMap;
	const uint8_t *wllWallFlags;
	const int8_t *wllShapeMap;
	const uint16_t *vmp;
	int32_t vmpLen;
	const uint8_t *vcnBlocks;
	const uint8_t *vcnShift;
	int32_t vcnCount;
	const uint8_t *vcnColTable;
	uint8_t palette[384];
	uint32_t wallCaptureSerial;
	int16_t wallSlot[256];
	int32_t wallSlots;
	const uint8_t *wallPixels;
	const uint8_t *wallAlpha;
	uint8_t screenPalette[768];
	int32_t nearObjects;
	const uint8_t *doorPixels;
	const uint8_t *doorAlpha;
	uint32_t spriteSerial;
	int32_t spriteCount;
	LolvrSprite sprites[32];
	const uint8_t *spritePixels;
	const uint8_t *spriteAlpha;
	int32_t sceneBusy;
	int32_t light;
	int32_t spellBusy;
	uint32_t spellSerial;
	const uint8_t *spellPixels;
	const uint8_t *spellAlpha;
	int32_t textBox;
	int32_t itemInHand;
	uint32_t cursorSerial;
	int32_t cursorW, cursorH, cursorHotX, cursorHotY, cursorKey;
	uint8_t cursorPixels[64 * 64];
	uint8_t cursorPalette[768];
	uint8_t doorRaise[256];
	uint32_t flightSerial;
	LolvrFlight flights[8];
	int32_t magicMenu;
	int32_t magicLevels;
	uint32_t placeSerial;
	int32_t placeCount;
	int32_t placeType[16];
	const uint8_t *placeRgb;
	int32_t scenePicture;
	uint32_t sceneRect;
	int32_t spellCount;
	uint32_t mapSerial;
	int32_t mapAvailable;
	uint8_t mapFlags[1024];
	const uint8_t *mapClass;
	char mapTitle[40];
	int32_t mapLegendCount;
	uint16_t mapLegend[32][3];
	char mapLegendText[32][40];
	const uint8_t *mapRgb;
	uint32_t invSerial;
	const uint8_t *invRgb;
	uint32_t animBusy;
	int32_t lamp;
};
const LolvrLevelData *retro_lolvr_level(void);
}

namespace lolvr {

namespace {

// Identifiant privé reconnu par notre correctif du cœur (pointeur absolu VR).
const unsigned kLolvrPointerId = 0x4C56;

struct KeyEvent {
	unsigned key;
	bool down;
	unsigned mods;
};

struct State {
	HostConfig cfg;
	std::thread thread;
	std::atomic<bool> quit{false};
	std::atomic<bool> running{false};
	std::atomic<bool> finished{false};
	std::atomic<bool> paused{false};

	// Niveau en cours (donjon en 3D), copié par le fil du cœur entre deux images
	std::mutex levelMutex;
	LevelSnapshot level;

	// Vidéo
	std::mutex frameMutex;
	Frame frame;
	retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;

	// Entrées
	std::atomic<int> pointerX{0};
	std::atomic<int> pointerY{0};
	std::atomic<bool> mouseLeft{false};
	std::atomic<bool> mouseRight{false};
	std::atomic<bool> padSelect{false};
	std::atomic<bool> padStart{false};
	std::mutex keyMutex;
	std::vector<KeyEvent> keyQueue;
	retro_keyboard_event_t keyboardCb = nullptr;

	// Audio : tampon circulaire de trames stéréo
	std::mutex audioMutex;
	std::vector<int16_t> audioRing;
	size_t audioRead = 0;
	size_t audioCount = 0; // en trames
	bool audioPrimed = false;
	double sampleRate = 48000.0;
	double fps = 60.0;
};

State g;

const size_t kAudioRingFrames = 48000; // 1 seconde
const size_t kAudioPrimeFrames = 3200; // ~65 ms avant de commencer la lecture
const size_t kAudioMaxLagFrames = 9600; // au-delà de 200 ms de retard, on rattrape

void logf(int level, const char *fmt, ...) {
	if (!g.cfg.log)
		return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g.cfg.log(level, buf);
}

void RETRO_CALLCONV coreLog(enum retro_log_level level, const char *fmt, ...) {
	if (!g.cfg.log)
		return;
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	g.cfg.log((int)level, buf);
}

struct Variable {
	const char *key;
	const char *value;
};

// Réglages du cœur ScummVM. Tout le reste garde sa valeur par défaut.
const Variable kVariables[] = {
	{"scummvm_video_hw_acceleration", "disabled"},
	{"scummvm_browsing_mode", "local"},
	{"scummvm_framerate", "60 Hz"},
	{"scummvm_samplerate", "48000 Hz"},
	{"scummvm_gui_aspect_ratio", "0"},
	{"scummvm_gui_h_res", "480"},
	// Deux boutons de la « manette » libretro servent de raccourcis ScummVM.
	{"scummvm_mapper_select", "RETROKE_VKBD"},
	{"scummvm_mapper_start", "RETROKE_SCUMMVM_GUI"},
};

bool RETRO_CALLCONV environment(unsigned cmd, void *data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((retro_log_callback *)data)->log = coreLog;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		retro_pixel_format fmt = *(const retro_pixel_format *)data;
		if (fmt != RETRO_PIXEL_FORMAT_RGB565 && fmt != RETRO_PIXEL_FORMAT_XRGB8888)
			return false;
		g.pixelFormat = fmt;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
		*(const char **)data = g.cfg.systemDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = g.cfg.saveDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		retro_variable *var = (retro_variable *)data;
		if (!var || !var->key)
			return false;
		for (const Variable &v : kVariables) {
			if (!strcmp(v.key, var->key)) {
				var->value = v.value;
				return true;
			}
		}
		var->value = nullptr;
		return false;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		g.keyboardCb = ((const retro_keyboard_callback *)data)->callback;
		return true;
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
		const retro_system_av_info *info = (const retro_system_av_info *)data;
		if (info && info->timing.fps > 1.0)
			g.fps = info->timing.fps;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE: {
		const retro_message *msg = (const retro_message *)data;
		if (msg && msg->msg)
			logf(RETRO_LOG_INFO, "[message] %s", msg->msg);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
		const retro_message_ext *msg = (const retro_message_ext *)data;
		if (msg && msg->msg)
			logf(RETRO_LOG_INFO, "[message] %s", msg->msg);
		return true;
	}
	case RETRO_ENVIRONMENT_SHUTDOWN:
		g.finished = true;
		return true;
	default:
		// Tout le reste (accélération matérielle, VFS, MIDI, options…) n'est pas fourni :
		// le cœur se rabat alors sur ses chemins logiciels par défaut.
		return false;
	}
}

inline void put(uint8_t *dst, uint8_t r, uint8_t gg, uint8_t b) {
	dst[0] = r;
	dst[1] = gg;
	dst[2] = b;
	dst[3] = 255;
}

void RETRO_CALLCONV videoRefresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || !width || !height)
		return;
	std::lock_guard<std::mutex> lock(g.frameMutex);
	Frame &f = g.frame;
	if (f.width != (int)width || f.height != (int)height) {
		f.width = (int)width;
		f.height = (int)height;
		f.rgba.assign((size_t)width * height * 4, 0);
	}
	uint8_t *dst = f.rgba.data();
	if (g.pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
		for (unsigned y = 0; y < height; y++) {
			const uint16_t *src = (const uint16_t *)((const uint8_t *)data + y * pitch);
			for (unsigned x = 0; x < width; x++, dst += 4) {
				uint16_t p = src[x];
				uint8_t r = (p >> 11) & 0x1f, gg = (p >> 5) & 0x3f, b = p & 0x1f;
				put(dst, (uint8_t)((r << 3) | (r >> 2)), (uint8_t)((gg << 2) | (gg >> 4)), (uint8_t)((b << 3) | (b >> 2)));
			}
		}
	} else {
		for (unsigned y = 0; y < height; y++) {
			const uint32_t *src = (const uint32_t *)((const uint8_t *)data + y * pitch);
			for (unsigned x = 0; x < width; x++, dst += 4) {
				uint32_t p = src[x];
				put(dst, (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p);
			}
		}
	}
	f.serial++;
}

size_t RETRO_CALLCONV audioBatch(const int16_t *data, size_t frames) {
	if (!data || !frames)
		return frames;
	std::lock_guard<std::mutex> lock(g.audioMutex);
	if (g.audioRing.empty())
		g.audioRing.assign(kAudioRingFrames * 2, 0);
	for (size_t i = 0; i < frames; i++) {
		if (g.audioCount == kAudioRingFrames) { // plein : on écrase le plus ancien
			g.audioRead = (g.audioRead + 1) % kAudioRingFrames;
			g.audioCount--;
		}
		size_t w = (g.audioRead + g.audioCount) % kAudioRingFrames;
		g.audioRing[w * 2] = data[i * 2];
		g.audioRing[w * 2 + 1] = data[i * 2 + 1];
		g.audioCount++;
	}
	return frames;
}

void RETRO_CALLCONV audioSample(int16_t left, int16_t right) {
	int16_t s[2] = {left, right};
	audioBatch(s, 1);
}

void RETRO_CALLCONV inputPoll() {}

int16_t RETRO_CALLCONV inputState(unsigned port, unsigned device, unsigned /*index*/, unsigned id) {
	if (port != 0)
		return 0;
	switch (device) {
	case RETRO_DEVICE_JOYPAD:
		if (id == RETRO_DEVICE_ID_JOYPAD_SELECT)
			return g.padSelect.load() ? 1 : 0;
		if (id == RETRO_DEVICE_ID_JOYPAD_START)
			return g.padStart.load() ? 1 : 0;
		return 0;
	case RETRO_DEVICE_POINTER:
		if (id == kLolvrPointerId)
			return 1;
		if (id == RETRO_DEVICE_ID_POINTER_X)
			return (int16_t)g.pointerX.load();
		if (id == RETRO_DEVICE_ID_POINTER_Y)
			return (int16_t)g.pointerY.load();
		return 0;
	case RETRO_DEVICE_MOUSE:
		if (id == RETRO_DEVICE_ID_MOUSE_LEFT)
			return g.mouseLeft.load() ? 1 : 0;
		if (id == RETRO_DEVICE_ID_MOUSE_RIGHT)
			return g.mouseRight.load() ? 1 : 0;
		return 0;
	default:
		return 0;
	}
}

double nowSeconds() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void sleepUntil(double t) {
	double d = t - nowSeconds();
	if (d <= 0)
		return;
	struct timespec ts;
	ts.tv_sec = (time_t)d;
	ts.tv_nsec = (long)((d - (double)ts.tv_sec) * 1e9);
	nanosleep(&ts, nullptr);
}

// Copie le niveau publié par le moteur. Appelée par le fil du cœur juste après retro_run() : le
// moteur est alors arrêté, ses tableaux ne bougent pas pendant la copie.
void copyLevel() {
	const LolvrLevelData *s = retro_lolvr_level();
	if (!s || s->version != 1)
		return;
	std::lock_guard<std::mutex> lock(g.levelMutex);
	LevelSnapshot &l = g.level;
	l.level = s->level;
	l.block = s->block;
	l.direction = s->direction;
	l.nearObjects = s->nearObjects;
	l.sceneBusy = s->sceneBusy != 0;
	l.textBox = s->textBox != 0;
	l.textRect = (uint32_t)s->textBox;
	// L'objet tenu par le pointeur, et l'image du curseur du jeu (en couleurs, le fond transparent)
	l.itemInHand = s->itemInHand;
	l.scenePicture = s->scenePicture != 0;
	l.sceneRect = s->sceneRect;
	l.spellCount = s->spellCount;
	// L'atlas tel que le groupe l'a exploré (fenêtre de la carte)
	if (l.map.serial != s->mapSerial) {
		MapSnapshot &m = l.map;
		m.serial = s->mapSerial;
		m.available = s->mapAvailable != 0;
		memcpy(m.flags, s->mapFlags, sizeof(m.flags));
		if (s->mapClass)
			memcpy(m.cls, s->mapClass, sizeof(m.cls));
		else
			memset(m.cls, 0x1F, sizeof(m.cls));
		m.title.assign(s->mapTitle, strnlen(s->mapTitle, sizeof(s->mapTitle)));
		if (s->mapRgb)
			m.rgb.assign(s->mapRgb, s->mapRgb + 320 * 200 * 3);
		m.legend.clear();
		for (int i = 0; i < s->mapLegendCount && i < 32; i++) {
			MapSnapshot::Legend e;
			e.trigger = s->mapLegend[i][0];
			e.mark = s->mapLegend[i][1];
			e.symbol = s->mapLegend[i][2];
			e.text.assign(s->mapLegendText[i], strnlen(s->mapLegendText[i], 40));
			m.legend.push_back(e);
		}
	}
	l.animBusy = s->animBusy != 0;
	l.lamp = s->lamp;
	if (s->invRgb && l.invSerial != s->invSerial) {
		l.invSerial = s->invSerial;
		l.invRgb.assign(s->invRgb, s->invRgb + 176 * 132 * 3);
	}
	l.magicMenu = s->magicMenu;
	l.magicLevels = s->magicLevels;
	// Les images des lieux (176 x 120, RVB), à poser sur leur mur
	if (l.placeSerial != s->placeSerial) {
		l.placeSerial = s->placeSerial;
		const int n = s->placeRgb && s->placeCount > 0 ? (s->placeCount > 16 ? 16 : s->placeCount) : 0;
		l.placeTypes.assign(s->placeType, s->placeType + n);
		l.placeRgb.assign(s->placeRgb ? s->placeRgb : (const uint8_t *)"", s->placeRgb ? s->placeRgb + (size_t)n * 176 * 120 * 3 : (const uint8_t *)"");
	}
	// Les objets en vol, à chaque pas
	if (l.flightSerial != s->flightSerial) {
		for (int i = 0; i < 8; i++) {
			l.flights[i].item = s->flights[i].item;
			l.flights[i].x = s->flights[i].x;
			l.flights[i].y = s->flights[i].y;
			l.flights[i].height = s->flights[i].height;
			l.flights[i].dir = s->flights[i].dir;
		}
		l.flightSerial = s->flightSerial;
	}
	if (s->cursorSerial != l.cursorSerial && s->cursorW > 0 && s->cursorH > 0 && s->cursorW <= 64 && s->cursorH <= 64) {
		const int n = s->cursorW * s->cursorH;
		l.cursorRgba.resize((size_t)n * 4);
		for (int i = 0; i < n; i++) {
			const int c = s->cursorPixels[i];
			uint8_t *o = &l.cursorRgba[(size_t)i * 4];
			for (int k = 0; k < 3; k++) {
				const int v = s->cursorPalette[c * 3 + k] & 63;
				o[k] = (uint8_t)((v << 2) | (v >> 4));
			}
			o[3] = c == s->cursorKey ? 0 : 255;
		}
		l.cursorW = s->cursorW;
		l.cursorH = s->cursorH;
		l.cursorHotX = s->cursorHotX;
		l.cursorHotY = s->cursorHotY;
		l.cursorSerial = s->cursorSerial;
	}
	l.light = s->light > 256 ? 256 : s->light < 0 ? 0 : s->light;
	// Les effets d'un sort en cours, dessinés par-dessus la vue du jeu
	l.spellBusy = s->spellBusy != 0 && s->spellPixels && s->spellAlpha;
	if (l.spellBusy && s->spellSerial != l.spellSerial) {
		l.spellPixels.assign(s->spellPixels, s->spellPixels + 176 * 120);
		l.spellAlpha.assign(s->spellAlpha, s->spellAlpha + 176 * 120);
	}
	l.spellSerial = s->spellSerial;
	// Monstres et objets proches, tels que le moteur vient de les dessiner
	if (s->spriteSerial != l.spriteSerial && s->spritePixels && s->spriteAlpha && s->spriteCount >= 0 && s->spriteCount <= 32) {
		const size_t one = (size_t)kSpriteW * kSpriteH;
		l.sprites.resize(s->spriteCount);
		l.spritePixels.resize(s->spriteCount * one);
		l.spriteAlpha.resize(s->spriteCount * one);
		for (int i = 0; i < s->spriteCount; i++) {
			const LolvrSprite &a = s->sprites[i];
			LevelSprite &b = l.sprites[i];
			b.x = a.x; b.y = a.y; b.kind = a.kind; b.ceiling = a.ceiling;
			b.x0 = a.x0; b.y0 = a.y0; b.x1 = a.x1; b.y1 = a.y1;
			b.version = a.version; b.id = a.id;
			b.flying = a.flying; b.flightDir = a.flightDir;
		}
		if (s->spriteCount) {
			memcpy(l.spritePixels.data(), s->spritePixels, s->spriteCount * one);
			memcpy(l.spriteAlpha.data(), s->spriteAlpha, s->spriteCount * one);
		}
		l.spriteSerial = s->spriteSerial;
	}
	if (l.wallsSerial != s->wallsSerial) {
		memcpy(l.walls, s->walls, sizeof(l.walls));
		l.wallsSerial = s->wallsSerial;
	}
	// Les murs dessinés par le moteur (décors et portes compris), une fois par niveau
	if (s->wallCaptureSerial && s->wallCaptureSerial == s->gfxSerial && l.wallCaptureSerial != s->wallCaptureSerial &&
	    l.gfxSerial == s->gfxSerial && s->wallPixels && s->wallAlpha && s->wallSlots > 0) {
		const size_t n = (size_t)s->wallSlots * 128 * 96;
		l.wallPixels.assign(s->wallPixels, s->wallPixels + n);
		l.wallAlpha.assign(s->wallAlpha, s->wallAlpha + n);
		if (s->doorPixels && s->doorAlpha) {
			l.doorPixels.assign(s->doorPixels, s->doorPixels + n);
			l.doorAlpha.assign(s->doorAlpha, s->doorAlpha + n);
		} else {
			l.doorPixels.clear();
			l.doorAlpha.clear();
		}
		memcpy(l.wallSlot, s->wallSlot, sizeof(l.wallSlot));
		memcpy(l.doorRaise, s->doorRaise, sizeof(l.doorRaise));
		memcpy(l.screenPalette, s->screenPalette, sizeof(l.screenPalette));
		l.wallSlots = s->wallSlots;
		l.wallCaptureSerial = s->wallCaptureSerial;
	}
	if (l.gfxSerial != s->gfxSerial && s->vmp && s->vcnBlocks && s->vcnColTable && s->wllVmpMap && s->wllWallFlags &&
	    s->vmpLen > 0 && s->vcnCount > 0) {
		l.vmp.assign(s->vmp, s->vmp + s->vmpLen);
		l.vcn.assign(s->vcnBlocks, s->vcnBlocks + (size_t)s->vcnCount * 32);
		if (s->vcnShift)
			l.vcnShift.assign(s->vcnShift, s->vcnShift + s->vcnCount);
		else
			l.vcnShift.assign((size_t)s->vcnCount, 0);
		memcpy(l.colTable, s->vcnColTable, sizeof(l.colTable));
		memcpy(l.palette, s->palette, sizeof(l.palette));
		memcpy(l.vmpMap, s->wllVmpMap, sizeof(l.vmpMap));
		memcpy(l.wallFlags, s->wllWallFlags, sizeof(l.wallFlags));
		l.gfxSerial = s->gfxSerial;
		l.wallCaptureSerial = 0;
		// Les murs aussi, au cas où ils n'auraient pas encore changé de numéro
		memcpy(l.walls, s->walls, sizeof(l.walls));
		l.wallsSerial = s->wallsSerial;
	}
}

void coreThread() {
	retro_set_environment(environment);
	retro_set_video_refresh(videoRefresh);
	retro_set_audio_sample(audioSample);
	retro_set_audio_sample_batch(audioBatch);
	retro_set_input_poll(inputPoll);
	retro_set_input_state(inputState);

	retro_init();

	bool loaded;
	// La langue choisie dans le casque, pour le jeu qui démarre
	if (g.cfg.language >= 0)
		retro_lolvr_set_lang(g.cfg.language);
	if (g.cfg.gameDir.empty()) {
		loaded = retro_load_game(nullptr);
	} else {
		retro_game_info info;
		memset(&info, 0, sizeof(info));
		info.path = g.cfg.gameDir.c_str();
		loaded = retro_load_game(&info);
	}

	if (!loaded) {
		logf(RETRO_LOG_ERROR, "[lol-vr] retro_load_game a échoué");
		g.finished = true;
	} else {
		retro_system_av_info av;
		memset(&av, 0, sizeof(av));
		retro_get_system_av_info(&av);
		if (av.timing.fps > 1.0)
			g.fps = av.timing.fps;
		if (av.timing.sample_rate > 1000.0)
			g.sampleRate = av.timing.sample_rate;
		logf(RETRO_LOG_INFO, "[lol-vr] cœur démarré : %.2f images/s, %.0f Hz", g.fps, g.sampleRate);

		double next = nowSeconds();
		std::vector<KeyEvent> keys;
		while (!g.quit && !g.finished) {
			if (g.paused) {
				sleepUntil(nowSeconds() + 0.02);
				next = nowSeconds();
				continue;
			}
			{
				std::lock_guard<std::mutex> lock(g.keyMutex);
				keys.swap(g.keyQueue);
			}
			if (g.keyboardCb) {
				for (const KeyEvent &k : keys)
					g.keyboardCb(k.down, k.key, 0, (uint16_t)k.mods);
			}
			keys.clear();

			retro_run();
			copyLevel();

			next += 1.0 / g.fps;
			double now = nowSeconds();
			if (next < now - 0.25) // gros retard (pause, chargement) : on repart de maintenant
				next = now;
			sleepUntil(next);
		}
		retro_unload_game();
	}

	retro_deinit();
	g.running = false;
}

} // namespace

CoreHost &CoreHost::get() {
	static CoreHost host;
	return host;
}

void CoreHost::setGameLanguage(int language) { retro_lolvr_set_lang(language); }
void CoreHost::inventoryEnable() { retro_lolvr_inventory(0, 0); }
void CoreHost::inventoryClick(int slot) { retro_lolvr_inventory(1, slot); }
void CoreHost::inventorySort() { retro_lolvr_inventory(2, 0); }
void CoreHost::nextSpell() { retro_lolvr_inventory(3, 0); }
void CoreHost::setOwnCursor(bool own) { retro_lolvr_hide_cursor(own ? 1 : 0); }
bool CoreHost::cursorShown() const { return retro_lolvr_cursor_shown() != 0; }

bool CoreHost::start(const HostConfig &cfg) {
	if (g.running)
		return false;
	g.cfg = cfg;
	g.quit = false;
	g.finished = false;
	g.running = true;
	g.thread = std::thread(coreThread);
	return true;
}

void CoreHost::stop() {
	g.quit = true;
	if (g.thread.joinable())
		g.thread.join();
	g.running = false;
}

bool CoreHost::running() const {
	return g.running;
}

bool CoreHost::finished() const {
	return g.finished;
}

bool CoreHost::fetchFrame(Frame &out) {
	std::lock_guard<std::mutex> lock(g.frameMutex);
	if (g.frame.serial == 0 || g.frame.serial == out.serial)
		return false;
	out = g.frame;
	return true;
}

void CoreHost::setPointer(float u, float v) {
	if (u < 0.f)
		u = 0.f;
	if (u > 1.f)
		u = 1.f;
	if (v < 0.f)
		v = 0.f;
	if (v > 1.f)
		v = 1.f;
	g.pointerX = (int)(u * 65534.0f) - 0x7fff;
	g.pointerY = (int)(v * 65534.0f) - 0x7fff;
}

void CoreHost::setMouseButtons(bool left, bool right) {
	g.mouseLeft = left;
	g.mouseRight = right;
}

void CoreHost::key(unsigned retroKey, bool down, unsigned retroMods) {
	std::lock_guard<std::mutex> lock(g.keyMutex);
	g.keyQueue.push_back({retroKey, down, retroMods});
}

void CoreHost::setSpecialButtons(bool virtualKeyboard, bool mainMenu) {
	g.padSelect = virtualKeyboard;
	g.padStart = mainMenu;
}

void CoreHost::setPaused(bool paused) {
	g.paused = paused;
}

void CoreHost::readAudio(int16_t *out, size_t frames) {
	std::lock_guard<std::mutex> lock(g.audioMutex);
	if (!g.audioPrimed && g.audioCount >= kAudioPrimeFrames)
		g.audioPrimed = true;
	// Si le jeu a pris trop d'avance sur la sortie son, on saute pour rester synchronisé.
	if (g.audioCount > kAudioMaxLagFrames + frames) {
		size_t skip = g.audioCount - kAudioPrimeFrames - frames;
		g.audioRead = (g.audioRead + skip) % kAudioRingFrames;
		g.audioCount -= skip;
	}
	size_t n = 0;
	if (g.audioPrimed && !g.paused) {
		n = frames < g.audioCount ? frames : g.audioCount;
		for (size_t i = 0; i < n; i++) {
			size_t r = (g.audioRead + i) % kAudioRingFrames;
			out[i * 2] = g.audioRing[r * 2];
			out[i * 2 + 1] = g.audioRing[r * 2 + 1];
		}
		g.audioRead = (g.audioRead + n) % kAudioRingFrames;
		g.audioCount -= n;
		if (n < frames)
			g.audioPrimed = false; // tampon vide : on laisse se remplir avant de reprendre
	}
	if (n < frames)
		memset(out + n * 2, 0, (frames - n) * 2 * sizeof(int16_t));
}

bool CoreHost::gameState(GameState &out) const {
	if (!g.running || g.finished)
		return false;
	const LolvrGameState *s = retro_lolvr_state();
	if (!s || s->version != 1)
		return false;
	// Lecture sans verrou depuis un autre fil : les compteurs sont lus en premier, si bien
	// qu'un événement en cours d'écriture sera simplement vu à l'image suivante.
	out.sfxSerial = s->sfxSerial;
	out.hurtSerial = s->hurtSerial;
	out.hitSerial = s->hitSerial;
	out.level = s->level;
	out.sfxVolume = s->sfxVolume;
	out.hurtDamage = s->hurtDamage;
	out.hitDamage = s->hitDamage;
	out.loopSerial = (uint32_t)s->reserved[0];
	out.controlMode = s->reserved[1] & 0xFF;
	out.charSheet = (s->reserved[1] & 0x100) != 0;
	out.characters = s->reserved[3];
	out.automapOpen = (s->reserved[2] & 1) != 0;
	out.menuOpen = (s->reserved[2] & (2 | 4)) != 0; // menu du jeu, ou menu de ScummVM (jeu en pause)
	out.nameEntry = (s->reserved[2] & 8) != 0;
	out.vrMenuSerial = (s->reserved[2] >> 8) & 0xFF;
	out.gameLang = (s->reserved[2] & 0x40000) ? (s->reserved[2] >> 16) & 3 : -1;
	memcpy(out.tileset, s->tileset, sizeof(out.tileset));
	memcpy(out.sfxName, s->sfxName, sizeof(out.sfxName));
	out.tileset[sizeof(out.tileset) - 1] = 0;
	out.sfxName[sizeof(out.sfxName) - 1] = 0;
	return true;
}

bool CoreHost::levelSnapshot(LevelSnapshot &out) const {
	if (!g.running || g.finished)
		return false;
	std::lock_guard<std::mutex> lock(g.levelMutex);
	const LevelSnapshot &l = g.level;
	if (l.gfxSerial == 0)
		return false;
	out.level = l.level;
	out.block = l.block;
	out.direction = l.direction;
	out.nearObjects = l.nearObjects;
	out.sceneBusy = l.sceneBusy;
	out.textBox = l.textBox;
	out.textRect = l.textRect;
	out.itemInHand = l.itemInHand;
	out.scenePicture = l.scenePicture;
	out.sceneRect = l.sceneRect;
	out.spellCount = l.spellCount;
	if (out.map.serial != l.map.serial)
		out.map = l.map;
	out.animBusy = l.animBusy;
	out.lamp = l.lamp;
	if (out.invSerial != l.invSerial) {
		out.invSerial = l.invSerial;
		out.invRgb = l.invRgb;
	}
	out.magicMenu = l.magicMenu;
	out.magicLevels = l.magicLevels;
	if (out.placeSerial != l.placeSerial) {
		out.placeSerial = l.placeSerial;
		out.placeTypes = l.placeTypes;
		out.placeRgb = l.placeRgb;
	}
	if (out.flightSerial != l.flightSerial) {
		memcpy(out.flights, l.flights, sizeof(out.flights));
		out.flightSerial = l.flightSerial;
	}
	if (out.cursorSerial != l.cursorSerial) {
		out.cursorRgba = l.cursorRgba;
		out.cursorW = l.cursorW;
		out.cursorH = l.cursorH;
		out.cursorHotX = l.cursorHotX;
		out.cursorHotY = l.cursorHotY;
		out.cursorSerial = l.cursorSerial;
	}
	out.light = l.light;
	out.spellBusy = l.spellBusy;
	if (l.spellBusy && out.spellSerial != l.spellSerial) {
		out.spellPixels = l.spellPixels;
		out.spellAlpha = l.spellAlpha;
	}
	out.spellSerial = l.spellSerial;
	if (out.spriteSerial != l.spriteSerial) {
		out.sprites = l.sprites;
		out.spritePixels = l.spritePixels;
		out.spriteAlpha = l.spriteAlpha;
		out.spriteSerial = l.spriteSerial;
	}
	if (out.wallsSerial != l.wallsSerial || out.gfxSerial != l.gfxSerial) {
		memcpy(out.walls, l.walls, sizeof(out.walls));
		out.wallsSerial = l.wallsSerial;
	}
	if (out.wallCaptureSerial != l.wallCaptureSerial) {
		out.wallPixels = l.wallPixels;
		out.wallAlpha = l.wallAlpha;
		out.doorPixels = l.doorPixels;
		out.doorAlpha = l.doorAlpha;
		memcpy(out.wallSlot, l.wallSlot, sizeof(out.wallSlot));
		memcpy(out.doorRaise, l.doorRaise, sizeof(out.doorRaise));
		memcpy(out.screenPalette, l.screenPalette, sizeof(out.screenPalette));
		out.wallSlots = l.wallSlots;
		out.wallCaptureSerial = l.wallCaptureSerial;
	}
	if (out.gfxSerial != l.gfxSerial) {
		out.vmp = l.vmp;
		out.vcn = l.vcn;
		out.vcnShift = l.vcnShift;
		memcpy(out.colTable, l.colTable, sizeof(out.colTable));
		memcpy(out.palette, l.palette, sizeof(out.palette));
		memcpy(out.vmpMap, l.vmpMap, sizeof(out.vmpMap));
		memcpy(out.wallFlags, l.wallFlags, sizeof(out.wallFlags));
		out.gfxSerial = l.gfxSerial;
	}
	return true;
}

int CoreHost::sampleRate() const {
	return (int)g.sampleRate;
}

// ---------------------------------------------------------------------------
// Écran de message

namespace {

// Décode un caractère UTF-8 ; renvoie '?' hors Latin-1.
unsigned nextChar(const char *&s) {
	unsigned char c = (unsigned char)*s++;
	if (c < 0x80)
		return c;
	if ((c & 0xE0) == 0xC0 && (*s & 0xC0) == 0x80) {
		unsigned cp = ((c & 0x1Fu) << 6) | ((unsigned char)*s++ & 0x3Fu);
		return cp < 256 ? cp : '?';
	}
	while ((*s & 0xC0) == 0x80)
		s++;
	return '?';
}

void drawText(Frame &f, int x0, int y0, const char *text, int scale, uint8_t r, uint8_t gg, uint8_t b) {
	int x = x0, y = y0;
	while (*text) {
		unsigned cp = nextChar(text);
		if (cp == '\n') {
			x = x0;
			y += 18 * scale;
			continue;
		}
		if (cp < 32 || cp > 255)
			cp = '?';
		const uint8_t *glyph = kFont8x16[cp - 32];
		for (int gy = 0; gy < 16 * scale; gy++) {
			int py = y + gy;
			if (py < 0 || py >= f.height)
				continue;
			uint8_t bits = glyph[gy / scale];
			for (int gx = 0; gx < 8 * scale; gx++) {
				int px = x + gx;
				if (px < 0 || px >= f.width)
					continue;
				if (bits & (0x80 >> (gx / scale)))
					put(&f.rgba[((size_t)py * f.width + px) * 4], r, gg, b);
			}
		}
		x += 8 * scale;
	}
}

} // namespace

void renderMessage(Frame &out, const char *title, const char *body) {
	out.width = 640;
	out.height = 480;
	out.rgba.assign((size_t)out.width * out.height * 4, 0);
	for (int y = 0; y < out.height; y++) {
		for (int x = 0; x < out.width; x++) {
			bool border = x < 4 || y < 4 || x >= out.width - 4 || y >= out.height - 4;
			uint8_t *p = &out.rgba[((size_t)y * out.width + x) * 4];
			if (border)
				put(p, 200, 160, 60);
			else
				put(p, 16, 14, 28);
		}
	}
	drawText(out, 24, 24, title, 2, 240, 200, 90);
	drawText(out, 24, 84, body, 1, 230, 230, 230);
	out.serial++;
}

} // namespace lolvr
