// MainWindow.h - airImager's window: the three steps (Image, Drive, Write)
// side by side, with the progress of a write below them.
#ifndef AIRIMAGER_MAIN_WINDOW_H
#define AIRIMAGER_MAIN_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

#include "engine/Drive.h"
#include "engine/Format.h"
#include "engine/ImageSource.h"
#include "engine/WriteJob.h"

class BButton;
class BCheckBox;
class BFilePanel;
class BStatusBar;
class BStringView;
class ChecksumWorker;
class StepArrow;
class StepView;
class WriteWorker;


class MainWindow : public BWindow {
public:
						MainWindow(BRect frame, bool verify);
	virtual				~MainWindow();

	virtual	void		MessageReceived(BMessage* message);
	virtual	bool		QuitRequested();

	// Also used by scripting (the window is locked). Each returns false with
	// a sentence in error when it cannot be done.
			bool		SetImage(const char* path, BString& error);
			bool		SetTarget(const char* path, BString& error);
			// confirm: ask the user first (scripting does not).
			bool		StartWrite(bool confirm, BString& error);
			void		CancelWrite(bool confirm);

			BString		ImagePath() const;
			BString		TargetPath() const;
			BString		Status() const;
			BString		DriveList() const;
			bool		Verify() const;
			void		SetVerify(bool verify);
			bool		IsBusy() const { return fWriter != NULL; }

private:
	enum CheckState {
		kNoCheck, kChecking, kCheckPassed, kCheckFailed, kCheckUnreadable
	};

			void		_BuildMenu(BView* menuBar);
			void		_ScanDrives();
			const imager::DriveInfo* _Target() const;
			void		_UpdateSteps();
			void		_UpdateDriveWindow();
			void		_OpenImagePanel();
			void		_OpenDriveWindow();
			void		_DriveChosen(int32 id);
			void		_ConfirmedWrite();
			void		_JobProgress(BMessage* message);
			void		_JobFinished(BMessage* message);
			void		_CheckProgress(BMessage* message);
			void		_CheckFinished(BMessage* message);
			void		_StopChecksum();
			void		_SetBusy(bool busy);
			void		_ShowIdleStatus();
			void		_Notify(const char* title, const char* text);

			StepView*	fImageStep;
			StepView*	fDriveStep;
			StepView*	fWriteStep;
			StepArrow*	fArrows[2];
			BButton*	fImageButton;
			BButton*	fDriveButton;
			BButton*	fWriteButton;
			BCheckBox*	fVerifyBox;
			BStatusBar*	fProgress;
			BStringView* fProgressDetail;
			BButton*	fCancelButton;
			BFilePanel*	fImagePanel;
			BMessenger	fDriveWindow;

			bool		fHaveImage;
			imager::ImageInfo fImage;
			std::vector<imager::DriveInfo> fDrives;
			int32		fTargetID;
			imager::DriveInfo fWriting;		// the drive being written

			CheckState	fCheckState;
			BString		fCheckMessage;
			float		fCheckPercent;
			ChecksumWorker* fChecker;

			WriteWorker* fWriter;
			int32		fJob;
			int32		fPhase;
			bigtime_t	fPhaseStart;
			imager::RateMeter fMeter;
			BString		fStatus;
			BString		fLastResult;
			bool		fQuitWhenStopped;
};

#endif
