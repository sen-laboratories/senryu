#include <Application.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>
#include <cstdio>
#include <ctime>
#include <map>
#include <String.h>
#include <Directory.h>
#include <Path.h>

// Storage for our three circuits
std::map<BString, BBitmap*> fCircuits;

const int32  kMsgTick = 'tick';
const BPoint kSenOffset(0, 0);
const BPoint kRyuOffset(230, 0);

class NeonView : public BView {
public:
	NeonView(BRect frame) : BView(frame, "NeonView", B_FOLLOW_ALL, B_WILL_DRAW),
		fStage(0), fAutoMode(false), fFlickerFrame(0), fFadeAlpha(0.0) {
		SetViewColor(51, 102, 152); // Haiku Blue background
		srand(time(NULL));
		InitAssets("../../../../../../../../build/boot_splash/");
	}

	void InitAssets(const char* assetPath) {
		BDirectory dir(assetPath);
		BEntry entry;

		while (dir.GetNextEntry(&entry) == B_OK) {
			BPath path;
			entry.GetPath(&path);
			BString fileName = path.Leaf();

			// Pattern: sen_0_warm.png, ryu_2_overdrive.png, etc.
			if (fileName.EndsWith(".png")) {
				entry_ref ref;
				entry.GetRef(&ref);

				BBitmap* bitmap = BTranslationUtils::GetBitmap(&ref);
				if (bitmap) {
					// Remove extension for the map key (e.g., "sen_0_warm")
					fileName.Truncate(fileName.Length() - 4);
					fCircuits[fileName] = bitmap;
				}
			}
		}
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
		_BlitNeon("stage", fStage, "warm", BPoint(0.0, 0.0));

		// draw neon logo
		int stability = (fStage * 100) / 7;

		// 2. Circuit 1: SEN (Top-Left)
		if ((rand() % 100) < stability) {
			_BlitNeon("sen", -1, "warm", kSenOffset);
		} else if (fFlickerFrame % 4 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitNeon("sen", -1, "cold", kSenOffset);
			else if (roll == 1) _BlitNeon("sen", -1, "vibrant", kSenOffset);
		}

		// 3. Circuit 2: Ryu (Bottom-Right)
		if ((rand() % 100) < (stability - 10)) {
			_BlitNeon("ryu", -1, "warm", kRyuOffset);
		} else if (fFlickerFrame % 7 == 0) {
			int roll = rand() % 3;
			if (roll == 0) _BlitNeon("ryu", -1, "cold", kRyuOffset);
			else if (roll == 1) _BlitNeon("ryu", -1, "vibrant", kRyuOffset);
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
		} else {
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
