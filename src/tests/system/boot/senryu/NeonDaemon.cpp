#include <Application.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>
#include <cstdio>
#include <ctime>
#include <map>
#include <Directory.h>
#include <Path.h>
#include "../../../../../generated/build/boot_splash/NeonAssets.h"

// Storage for our three circuits
std::map<BString, BBitmap*> fCircuits;

const int32  kMsgTick = 'tick';

// Global circuit origins (Top-Left of the BBOXes)
const BPoint kSenOrigin(0, 0);
const BPoint kRyuOrigin(230, 0);
const BPoint kStageOrigin(482, 0);

// --- Configuration from Python Image Cutter ---
static const int kTotalStages = 8;

// Define our states as an enum for index-based lookup (last enum ~ count)
enum NeonState { COLD = 0, WARM, VIBRANT, OVERDRIVE, STATE_COUNT};
enum Circuit   { SEN = 0, RYU, CIRCUIT_COUNT };

// splash screen parts for all stages and color states
BBitmap* fStageBitmaps[kTotalStages][STATE_COUNT];

// circuits only change state, independent of stage (stage is indicated by flicker frequency)
BBitmap* fCircuitBitmaps[CIRCUIT_COUNT][STATE_COUNT];

BBitmap* fullSplash;

static const uint32 kSenBase16[] = {
    0x00000000, // 0: Pure Black (Transparent)
    0xFF2F2F2F, // 1: Dark Grey
    0xFF505050, // 2: Mid Grey
    0xFF78B4FF, // 3: Primary Blue (Neon)
    0xFF3C5AB4, // 4: Deep Blue (Gradient)
    0xFF2EBFD4, // 5: Scooter (Teal)
    0xFF14646E, // 6: Deep Teal
    0xFFF4F4F4, // 7: Wild Sand (White-ish)
    0xFFF7F2E1, // 8: Cream
    0xFFFF33CC, // 9: Razzle Rose (Magenta)
    0xFF960064, // 10: Deep Rose
    0xFF64FF64, // 11: Accent Green
    0xFFFFA500, // 12: Accent Orange
    0xFFC8C8C8, // 13: Light Grey
    0xFF1E1E3C, // 14: Midnight Blue (Shadows)
    0xFFFFFFFF  // 15: Pure White
};

uint32 kSenPalette256[256];

// Bounding Box Dimensions (Width/Height)
#define sen_rect   BRect(0, 0, 226 - 1, 112 - 1)
#define ryu_rect   BRect(0, 0, 176 - 1, 182 - 1)
#define stage_rect BRect(0, 0, 542 - 1, 388 - 1)

// --- The Neon Schema ---
#define FOR_EACH_STATE(DO, circuit, stage) \
DO(circuit, stage, cold) \
DO(circuit, stage, warm) \
DO(circuit, stage, vibrant) \
DO(circuit, stage, overdrive)

#define FOR_EACH_STATIC_STATE(DO, circuit) \
DO(circuit, cold) \
DO(circuit, warm) \
DO(circuit, vibrant) \
DO(circuit, overdrive)

class NeonView : public BView {
public:
	NeonView(BRect frame) : BView(frame, "NeonView", B_FOLLOW_ALL, B_WILL_DRAW),
		fStage(0), fAutoMode(false), fFlickerFrame(0), fFadeAlpha(0.0) {

		SetViewColor(51, 102, 152); // Haiku Blue background
		srand(time(nullptr));

		InitializeSenPalette();
		InitAssets();
	}

	void InitializeSenPalette() {
		// 1. Copy the first 16 base colors
		for (int i = 0; i < 16; i++) kSenPalette256[i] = kSenBase16[i];

		// 2. Helper to generate ramps (Linear Interpolation)
		auto lerp = [](uint32 c1, uint32 c2, int steps, int startSlot) {
			uint8 r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
			uint8 r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;

			for (int i = 0; i < steps; i++) {
				float r = i / (float)steps;
				uint8 nr = r1 + (r2 - r1) * r;
				uint8 ng = g1 + (g2 - g1) * r;
				uint8 nb = b1 + (b2 - b1) * r;
				kSenPalette256[startSlot + i] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
			}
		};

		// 3. Fill the "Stepping Stone" slots for gradients
		lerp(kSenBase16[3], kSenBase16[4], 32, 16);  // Blue Neon Ramps
		lerp(kSenBase16[9], kSenBase16[10], 32, 48); // Rose Neon Ramps
		lerp(kSenBase16[5], kSenBase16[6], 32, 80);  // Teal Neon Ramps
		lerp(kSenBase16[7], kSenBase16[15], 32, 112); // White Ramps (Slots 112-143)

		// Pad remainder with black
		for (int i = 144; i < 256; i++) {
		    kSenPalette256[i] = 0xFF000000;
		}
	}

	BBitmap* CreateBitmapFromRaw(const unsigned char* data, size_t length, BRect bbox)
	{
		BBitmap* bitmap = new BBitmap(bbox, B_RGBA32); //
		uint8* destBase = (uint8*)bitmap->Bits(); //
		int32 bpr = bitmap->BytesPerRow(); //
		int32 width = (int32)bbox.Width() + 1; //
		int32 height = (int32)bbox.Height() + 1; //

		for (int y = 0; y < height; y++) {
			uint32* row = (uint32*)(destBase + (y * bpr)); //
			for (int x = 0; x < width; x++) {
				int srcIdx = (y * width) + x; //
				if (srcIdx < (int32)length) {
					// Direct 8-bit to 32-bit mapping
					row[x] = kSenPalette256[data[srcIdx]];
				}
			}
		}
		return bitmap;
	}

	void InitAssets()
	{
		// initialize fullscreen background
		fullSplash = CreateBitmapFromRaw(stage_0, stage_0_len, BRect(0.0, 0.0, 1023.0, 767.0));

		// Macro to fill the arrays directly via pointer assignment
		#define FILL_STATIC(circuit, part) \
		fCircuitBitmaps[part][COLD]      = CreateBitmapFromRaw(circuit##_cold, circuit##_cold_len, circuit##_rect); \
		fCircuitBitmaps[part][WARM]      = CreateBitmapFromRaw(circuit##_warm, circuit##_warm_len, circuit##_rect); \
		fCircuitBitmaps[part][VIBRANT]   = CreateBitmapFromRaw(circuit##_vibrant, circuit##_vibrant_len, circuit##_rect); \
		fCircuitBitmaps[part][OVERDRIVE] = CreateBitmapFromRaw(circuit##_overdrive, circuit##_overdrive_len, circuit##_rect);

		#define FILL_STAGE(s) \
		fStageBitmaps[s][COLD]      = CreateBitmapFromRaw(stage_##s##_cold, stage_##s##_cold_len, stage_rect); \
		fStageBitmaps[s][WARM]      = CreateBitmapFromRaw(stage_##s##_warm, stage_##s##_warm_len, stage_rect); \
		fStageBitmaps[s][VIBRANT]   = CreateBitmapFromRaw(stage_##s##_vibrant, stage_##s##_vibrant_len, stage_rect); \
		fStageBitmaps[s][OVERDRIVE] = CreateBitmapFromRaw(stage_##s##_overdrive, stage_##s##_overdrive_len, stage_rect);

		FILL_STATIC(sen, SEN)
		FILL_STATIC(ryu, RYU)

		FILL_STAGE(0)
		FILL_STAGE(1)
		FILL_STAGE(2)
		FILL_STAGE(3)
		FILL_STAGE(4)
		FILL_STAGE(5)
		FILL_STAGE(6)
		FILL_STAGE(7)

		#undef FILL_STATIC
		#undef FILL_STAGE
	}

	void Draw(BRect updateRect) override {
		// In a real bootloader, this is a raw blit to the framebuffer.
		// Here we use DrawBitmap for the UI simulation.
		_DrawStage();
	}

	void Tick() {
		if (!fAutoMode) return;

		fFlickerFrame++;

		// Simulate module loading progress
		if (fFlickerFrame % 20 == 0 && fStage < 7) {
			fStage++;
		}

		if (fStage == 7 && fFadeAlpha < 1.0) {
			fFadeAlpha += 0.05; // Trigger the fade-to-white
		}

		Invalidate();
	}

    void NextStage() {
		fStage++;
		if (fStage > 7) fStage = 0;
	}

    void PrevStage() {
		fStage--;
		if (fStage < 0) fStage = 7;
	}

	void ToggleAuto() {
		fAutoMode = !fAutoMode;
		if (!fAutoMode) { fStage = 0; fFadeAlpha = 0.0; }
		Invalidate();
	}

private:
	void _DrawStage() {
		// draw initial full screen splash
		DrawBitmap(fullSplash, BPoint(0.0, 0.0));

		// 1. Draw Background for the current stage
		_BlitStage(fStage, WARM, kStageOrigin);

		// draw neon logo
		int stability = (fStage * 100) / 7;

		// 2. Circuit 1: SEN (Top-Left)
		if ((rand() % 100) < stability) {
			_BlitCircuit(SEN, WARM, kSenOrigin);
		} else if (fFlickerFrame % 4 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitCircuit(SEN, COLD, kSenOrigin);
			else if (roll == 1) _BlitCircuit(SEN, VIBRANT, kSenOrigin);
		}

		// 3. Circuit 2: Ryu (Bottom-Right)
		if ((rand() % 100) < (stability - 10)) {
			_BlitCircuit(RYU, WARM, kRyuOrigin);
		} else if (fFlickerFrame % 7 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitCircuit(RYU, COLD, kRyuOrigin);
			else if (roll == 1) _BlitCircuit(RYU, VIBRANT, kRyuOrigin);
		}

		// 4. Final Transition
		if (fFadeAlpha > 0.1) {
			SetHighColor(255, 255, 255, (uint8)(fFadeAlpha * 255));
			SetDrawingMode(B_OP_ALPHA);
			FillRect(Bounds());
			SetDrawingMode(B_OP_COPY);
		}
	}

	// Helper to handle the tinyxxd naming convention
	void _BlitCircuit(int part, int state, BPoint pt) {
		printf("render circuit %s in state %d at point %f,%f\n",
			part == SEN ? "SEN" : "RYU", state, pt.x, pt.y);

		DrawBitmap(fCircuitBitmaps[part][state], pt);
	}

	void _BlitStage(int stage, int state, BPoint pt) {
		printf("render stage %d in state %d at point %f,%f\n",
			stage, state, pt.x, pt.y);

		DrawBitmap(fStageBitmaps[stage][state], pt);
	}

	int    fStage;
	bool   fAutoMode;
	uint32 fFlickerFrame;
	float  fFadeAlpha;
};

class NeonWindow : public BWindow {
public:
	NeonWindow() : BWindow(BRect(100, 100, 1124, 868), "SENryu NeonDaemon",
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_QUIT_ON_WINDOW_CLOSE) {

		fView = new NeonView(Bounds());
		AddChild(fView);

		// Start the irregular "module loader" stimulation
		fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgTick), 50000);
	}

	void MessageReceived(BMessage* msg) override {
		switch (msg->what) {
			case kMsgTick:
				fView->Tick();
				// Randomize the next interval to simulate irregular disk I/O
				fRunner->SetInterval((rand() % 100000) + 20000);
				break;
			case B_KEY_DOWN:
				uint32 rawChar;
				msg->FindInt32("raw_char", (int32*)&rawChar);

				switch (rawChar) {
					case B_SPACE: fView->ToggleAuto(); break;
					case B_RIGHT_ARROW: fView->NextStage(); break;
					case B_LEFT_ARROW: fView->PrevStage(); break;
				}

				break;
			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	NeonView* fView;
	BMessageRunner* fRunner;
};

int main() {
	BApplication app("application/x-vnd.sen-labs.NeonDaemon");
	(new NeonWindow())->Show();
	app.Run();
	return 0;
}
