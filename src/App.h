// App.h - the application: settings, files opened with airImager, and
// scripting (hey airImager get Status, set Image to ..., do Write).
#ifndef AIRIMAGER_APP_H
#define AIRIMAGER_APP_H

#include <Application.h>
#include <String.h>

class MainWindow;

extern const char* kAppSignature;

// ~/config/settings/airImager: the window's place, "Verify after writing"
// and the last image (to open the file panel in its folder).
void SaveSettings(BRect frame, bool verify, const char* lastImage);
BString LastImagePath();


class App : public BApplication {
public:
						App();

	virtual	void		ReadyToRun();
	virtual	void		ArgvReceived(int32 argc, char** argv);
	virtual	void		RefsReceived(BMessage* message);
	virtual	void		AboutRequested();
	virtual	void		MessageReceived(BMessage* message);

	virtual	BHandler*	ResolveSpecifier(BMessage* message, int32 index,
							BMessage* specifier, int32 what,
							const char* property);
	virtual	status_t	GetSupportedSuites(BMessage* data);

private:
			void		_EnsureWindow();
			bool		_HandleScripting(BMessage* message);

			MainWindow*	fWindow;
			BString		fPendingImage;
};

#endif
