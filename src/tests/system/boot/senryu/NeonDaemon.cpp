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
		InitAssets();
	}

	/**
	 * Helper to create a BBitmap from tinyxxd raw data.
	 * Assumes the cutter outputs B_RGBA32 or similar raw format.
	 * Adjust the width/height to match your splash-image-cutter.py output.
	 */
	BBitmap* CreateBitmapFromRaw(const unsigned char* data, size_t length, BRect bbox)
	{
 		BBitmap* bitmap = new BBitmap(bbox, B_RGBA32);

		// Copy the raw pixel data into the bitmap
		status_t status = bitmap->ImportBits(data, length, B_ANY_BYTES_PER_ROW, 0, B_RGB32);
		if (status != B_OK) {
			printf("ImportBits failed: %s\n", strerror(status));
		}

		return bitmap;
	}

	void InitAssets()
	{
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
