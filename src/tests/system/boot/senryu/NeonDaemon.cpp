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
#include "NeonAssets.h"

// Storage for our three circuits
std::map<BString, BBitmap*> fCircuits;

const int32  kMsgTick = 'tick';
// Global circuit origins (Top-Left of the BBOXes)
const BPoint kSenOrigin(0, 0);
const BPoint kRyuOrigin(230, 0);
const BPoint kStageOrigin(482, 0);

// --- Configuration from Python Image Cutter ---
static const int kTotalStages = 7;

// Bounding Box Dimensions (Width/Height)
#define SEN_SIZE   BRect(0, 0, 226 - 1, 112 - 1)
#define RYU_SIZE   BRect(0, 0, 176 - 1, 182 - 1)
#define STAGE_SIZE BRect(0, 0, 542 - 1, 388 - 1)

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
	BBitmap* CreateBitmapFromRaw(const unsigned char* data, size_t length, float width, float height)
	{
		BRect rect(0, 0, width - 1, height - 1);
		BBitmap* bitmap = new BBitmap(rect, B_RGBA32);

		// Copy the raw pixel data into the bitmap
		if (bitmap->ImportData(data, length, 0, B_RGBA32) != B_OK) {
			// Fallback for older Haiku/simpler buffers
			bitmap->SetBits(data, length, 0, B_RGBA32);
		}

		return bitmap;
	}

	void NeonDaemon::InitAssets()
	{
		// 1. Register Static Circuits (sen, ryu) - No stages
		#define REGISTER_STATIC(circuit, state) \
		fCircuits[BString().SetToFormat("%s_%s", #circuit, #state)] = \
		CreateBitmapFromRaw(circuit##_##state##_png, \
		circuit##_##state##_png_len, \
		(BString(#circuit) == "sen" ? SEN_SIZE : RYU_SIZE));

		FOR_EACH_STATIC_STATE(REGISTER_STATIC, sen)
		FOR_EACH_STATIC_STATE(REGISTER_STATIC, ryu)

		// 2. Register Staged Circuit (wave_icons)
		#define REGISTER_STAGED(circuit, stage, state) \
			fCircuits[BString().SetToFormat("%s_%d_%s", #circuit, stage, #state)] = \
			CreateBitmapFromRaw(circuit##_##stage##_##state##_png, \
			circuit##_##stage##_##state##_png_len, STAGE_SIZE);

		for (int s = 0; s < kTotalStages; s++) {
			if (s == 0)      { FOR_EACH_STATE(REGISTER_STAGED, stage, 0) }
			else if (s == 1) { FOR_EACH_STATE(REGISTER_STAGED, stage, 1) }
			else if (s == 2) { FOR_EACH_STATE(REGISTER_STAGED, stage, 2) }
			else if (s == 3) { FOR_EACH_STATE(REGISTER_STAGED, stage, 3) }
		}

		#undef REGISTER_STATIC
		#undef REGISTER_STAGED
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
		_BlitNeon("stage_", fStage, "warm", kStageOrigin);

		// draw neon logo
		int stability = (fStage * 100) / 7;

		// 2. Circuit 1: SEN (Top-Left)
		if ((rand() % 100) < stability) {
			_BlitNeon("sen", -1, "warm", kSenOrigin);
		} else if (fFlickerFrame % 4 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitNeon("sen", -1, "cold", kSenOrigin);
			else if (roll == 1) _BlitNeon("sen", -1, "vibrant", kSenOrigin);
		}

		// 3. Circuit 2: Ryu (Bottom-Right)
		if ((rand() % 100) < (stability - 10)) {
			_BlitNeon("ryu", -1, "warm", kRyuOrigin);
		} else if (fFlickerFrame % 7 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitNeon("ryu", -1, "cold", kSenOrigin);
			else if (roll == 1) _BlitNeon("ryu", -1, "vibrant", kSenOrigin);
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
	void _BlitNeon(const char* part, int stage, const char* state, BPoint pt) {
		printf("render stage %d with part %s in state %s at point %f,%f\n",
			stage, part, state, pt.x, pt.y);

		BString key;
		if (stage >= 0) {    // wave
			key.SetToFormat("%s_%d_%s", part, stage, state); // states: "cold", "warm", etc.
		} else {			// SEN / ryu circuits
			key.SetToFormat("%s_%s", part, state); // states: "cold", "warm", etc.
		}

		if (fCircuits.count(key)) {
			DrawBitmap(fCircuits[key], pt);
		} else {
			printf("  x could not find bitmap for key %s\n", key.String());
		}
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
