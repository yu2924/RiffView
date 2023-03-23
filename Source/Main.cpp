//
//  Main.cpp
//  RiffView_App
//
//  created by yu2924 on 2023-03-22
//

#include <JuceHeader.h>
#include "MainComponent.h"

static const juce::LookAndFeel_V4::ColourScheme LightColourScheme =
{
	0xffffffff, // windowBackground
	0xffeeeef2, // widgetBackground
	0xfff6f6f6, // menuBackground
	0xffcccedb, // outline
	0xff000000, // defaultText
	0xffcccedb, // defaultFill
	0xffffffff, // highlightedText
	0xff006cbe, // highlightedFill
	0xff000000, // menuText
};

class MainWindow : public juce::DocumentWindow
{
private:
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
public:
	MainWindow(juce::String name)
		: DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), DocumentWindow::allButtons)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(MainComponentCreateInstance(), true);
#if JUCE_IOS || JUCE_ANDROID
		setFullScreen(true);
#else
		setResizable(true, true);
		centreWithSize(getWidth(), getHeight());
#endif
		setVisible(true);
	}
	virtual void closeButtonPressed() override
	{
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}
};

class RiffViewApplication : public juce::JUCEApplication
{
private:
	std::unique_ptr<MainWindow> mainWindow;
public:
	RiffViewApplication() {}
	virtual const juce::String getApplicationName() override { return ProjectInfo::projectName; }
	virtual const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
	virtual bool moreThanOneInstanceAllowed() override { return true; }
	virtual void initialise(const juce::String&) override
	{
		if(juce::LookAndFeel_V4* lf4 = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
		{
			lf4->setColourScheme(LightColourScheme);
		}
		mainWindow.reset(new MainWindow(getApplicationName()));
	}
	virtual void shutdown() override
	{
		mainWindow = nullptr;
	}
	virtual void systemRequestedQuit() override
	{
		quit();
	}
	virtual void anotherInstanceStarted(const juce::String&) override
	{
	}

};

START_JUCE_APPLICATION(RiffViewApplication)
