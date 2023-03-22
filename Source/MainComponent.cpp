//
//  MainComponent.cpp
//  RiffView_App
//
//  created by yu2924 on 2023-03-22
//

#include "MainComponent.h"
#include "riffrw.h"

class RiffNodeTempFile : public juce::ReferenceCountedObject
{
protected:
	juce::File tmpPath;
	const riffrw::RiffNode* node;
public:
	using Ptr = juce::ReferenceCountedObjectPtr<RiffNodeTempFile>;
	RiffNodeTempFile(const juce::File& srcpath, const riffrw::RiffNode* n) : node(n)
	{
		DBG("RiffNodeTempFile: construct");
		juce::String fn(node->ckinfo.pathElement());
		fn = fn.replaceCharacter(' ', '_');
		tmpPath = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(fn + ".riffck");
		juce::FileInputStream istr(srcpath);
		juce::FileOutputStream ostr(tmpPath);
		if(istr.openedOk() && ostr.openedOk())
		{
			ostr.setPosition(0);
			ostr.truncate();
			istr.setPosition(node->ckinfo.hdroffset + 8);
			std::array<char, 4096> buf;
			uint32_t len = node->ckinfo.header.cksize, pos = 0;
			while(pos < len)
			{
				uint32_t lseg = std::min(len - pos, (uint32_t)buf.size());
				istr.read(buf.data(), lseg);
				ostr.write(buf.data(), lseg);
				pos += lseg;
			}
			DBG("RiffNodeTempFile: path=" << tmpPath.getFullPathName().quoted() << " " << (int)len << " bytes");
		}
	}
	~RiffNodeTempFile()
	{
		if(tmpPath.exists()) tmpPath.deleteFile();
		DBG("RiffNodeTempFile: destruct");
	}
	juce::File getTempPath() const
	{
		return tmpPath;
	}
};

class RiffDocument
{
protected:
	juce::File contentPath;
	riffrw::RiffNode rootNode{};
public:
	RiffDocument()
	{
	}
	void clearContent()
	{
		contentPath = {};
		rootNode = {};
	}
	bool loadContent(const juce::File& path)
	{
		contentPath = {};
		rootNode = {};
		riffrw::RiffNode n = {};
		if(riffrw::RiffNode::readTreeFromFile(std::wstring(path.getFullPathName().toUTF16()), &n))
		{
			rootNode = n;
			contentPath = path;
		}
		return true;
	}
	const juce::File& getContentPath() const
	{
		return contentPath;
	}
	const riffrw::RiffNode& getRootNode() const
	{
		return rootNode;
	}
};

// ================================================================================
// HexViewPane

class HexViewPane : public juce::Component
{
protected:
	juce::Colour backgounrdColor{ 0xffffffff };
	juce::Colour textColor{ 0xff000000 };
	juce::File riffPath;
	const riffrw::RiffNode* node = nullptr;
	std::unique_ptr<juce::FileInputStream> inputStream;
	juce::Font fixedFont;
	int charHeight = 14;
	int charWidth = 8;
	int idealPaneWidth = 0;
	float charAscent = 14;
	bool contentTooLarge = false;
public:
	// columns: 00000000  00 11 22 33  44 55 66 77  88 99 aa bb  cc dd ee ff  cccccccccccccccc
	std::function<void(const HexViewPane*)> onMouseDrag;
	HexViewPane()
	{
		charHeight = 14;
		fixedFont = juce::Font(juce::Font::getDefaultMonospacedFontName(), (float)charHeight, juce::Font::plain);
		charWidth = juce::roundToInt(fixedFont.getStringWidthFloat("W") + 0.5f);
		idealPaneWidth = (8 + 2 + 12 + 1 + 12 + 1 + 12 + 1 + 12 + 2 + 16) * charWidth;
		charAscent = fixedFont.getAscent();
		updatePaneSize();
	}
	void updatePaneSize()
	{
		int64_t length = node ? node->ckinfo.header.cksize : 0;
		int numrows = (int)((length + 15) / 16);
		contentTooLarge = 65536 < numrows;
		int height = ((0 < numrows) && !contentTooLarge) ? (numrows * charHeight) : charHeight;
		if(juce::Viewport* vp = findParentComponentOfClass<juce::Viewport>())
		{
			height = std::max(vp->getMaximumVisibleHeight(), height);
		}
		setSize(idealPaneWidth, height);
		repaint();
	}
	virtual void paint(juce::Graphics& g) override
	{
		// NOTE: it is an extremely easy implementation and requires more optimization
		juce::Rectangle<int> rc = getLocalBounds();
		juce::Rectangle<int> rcclip = g.getClipBounds();
		g.setColour(backgounrdColor);
		g.fillAll();
		if(!node) return;
		g.setColour(textColor);
		g.setFont(fixedFont);
		int ascent = (int)(charAscent + 1);
		int64_t length = node ? node->ckinfo.header.cksize : 0;
		int numrows = (int)((length + 15) / 16);
		int rowfrom = rcclip.getY() / charHeight;
		if(numrows <= rowfrom) return;
		int rowthru = std::min(numrows - 1, (rcclip.getBottom() + charHeight - 1) / charHeight);
		if(rowthru < rowfrom) return;
		float ytop = (float)rowfrom * charHeight;
		float ybottom = (float)(rowthru + 1) * charHeight;
		static const float dash[] = { 4, (float)(charHeight / 2 - 4) };
		for(int x : { 8 + 1, 8 + 2 + 12, 8 + 2 + 12 + 1 + 12, 8 + 2 + 12 + 1 + 12 + 1 + 12, 8 + 2 + 12 + 1 + 12 + 1 + 12 + 1 + 12 })
		{
			float xf = (float)(x * charWidth) + 0.5f;
			g.drawDashedLine(juce::Line<float>(xf, ytop, xf, ybottom), dash, 2, 1, 0);
		}
		int64_t ckdataoffset = node->ckinfo.hdroffset + 8;
		inputStream->setPosition(ckdataoffset + rowfrom * 16);
		std::array<uint8_t, 16> buffer;
		uint32_t cksize = node->ckinfo.header.cksize, ckpos = 0;
		for(int row = rowfrom; row <= rowthru; ++row)
		{
			if(cksize <= ckpos) break;
			int lrow = (int)std::min((uint32_t)16, cksize - ckpos);
			inputStream->read(buffer.data(), lrow);
			int y = row * charHeight;
			// col: offset
			int xoff = 0;
			g.drawSingleLineText(juce::String::formatted("%08x", row * 16), xoff, y + ascent, juce::Justification::left);
			// col: hex
			int xhex = (8 + 2) * charWidth;
			for(int i = 0; i < 4; ++i)
			{
				if(lrow <= (i * 4)) break;
				for(int j = 0; j < 4; ++j)
				{
					if(lrow <= (i * 4 + j)) break;
					g.drawSingleLineText(juce::String::formatted("%02x ", buffer[i * 4 + j]), xhex, y + ascent, juce::Justification::left);
					xhex += 3 * charWidth;
				}
				xhex += charWidth;
			}
			// col: text
			int xtxt = (8 + 2 + 12 + 1 + 12 + 1 + 12 + 1 + 12 + 1) * charWidth;
			for(int i = 0; i < 16; ++i) { if(!buffer[i]) buffer[i] = '.'; }
			g.drawSingleLineText(juce::String(juce::CharPointer_UTF8((const char*)buffer.data()), lrow), xtxt, y + ascent, juce::Justification::left);
			ckpos += lrow;
		}
	}
	virtual void mouseDrag(const juce:: MouseEvent&) override
	{
		if(!node) return;
		if(onMouseDrag) onMouseDrag(this);
	}
	void clearRiffNode()
	{
		riffPath = {};
		node = nullptr;
		inputStream = nullptr;
		contentTooLarge = false;
		updatePaneSize();
	}
	bool setRiffNode(const juce::File& path, const riffrw::RiffNode* n)
	{
		clearRiffNode();
		std::unique_ptr<juce::FileInputStream> str = std::make_unique<juce::FileInputStream>(path);
		if(str->failedToOpen()) return false;
		inputStream = std::move(str);
		riffPath = path;
		node = n;
		updatePaneSize();
		return true;
	}
	const riffrw::RiffNode* getRiffNode() const
	{
		return node;
	}
	const int getIdealPaneWidth() const
	{
		return idealPaneWidth;
	}
};

// ================================================================================
// RiffNodeTreeView

class RiffNodeTVItem : public juce::TreeViewItem
{
protected:
	class ItemComponent : public juce::Component
	{
	public:
		std::function<void()> onMouseDrag;
		ItemComponent() {}
		virtual void paint(juce::Graphics&) override {}
		virtual void mouseDrag(const juce::MouseEvent&) override { if(onMouseDrag) onMouseDrag(); }
	};
	struct SharedImages
	{
		static juce::Image iconFromDrawable(const juce::Drawable* drw, int cx, int cy)
		{
			juce::Image img(juce::Image::ARGB, cx, cy, true);
			{
				juce::Graphics g(img);
				drw->drawWithin(g, { 0, 0, (float)cx, (float)cy }, juce::RectanglePlacement::onlyReduceInSize | juce::RectanglePlacement::centred, 1);
			}
			return img;
		}
		juce::Image folderIcon;
		juce::Image fileIcon;
		SharedImages()
		{
			if(juce::LookAndFeel_V2* lf2 = dynamic_cast<juce::LookAndFeel_V2*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
			{
				int cxy = RiffNodeItemHeight - 2;
				folderIcon = iconFromDrawable(lf2->getDefaultFolderImage(), cxy, cxy);
				fileIcon = iconFromDrawable(lf2->getDefaultDocumentFileImage(), cxy, cxy);
			}
		}
	};
	juce::SharedResourcePointer<SharedImages> sharedImages;
	const riffrw::RiffNode* node;
	enum { RiffNodeItemHeight = 20 };
public:
	std::function<void(const RiffNodeTVItem*)> onSelectionChanged;
	std::function<void(const RiffNodeTVItem*)> onMouseDrag;
	RiffNodeTVItem(const riffrw::RiffNode* n) : node(n) {}
	const riffrw::RiffNode* getRiffNode() const { return node; }
	virtual bool mightContainSubItems() override { return node->ckinfo.header.isContainer(); }
	virtual int getItemHeight() const override { return RiffNodeItemHeight; }
	virtual bool customComponentUsesTreeViewMouseHandler() const override { return true; }
	virtual std::unique_ptr<juce::Component> createItemComponent() override
	{
		ItemComponent* p = new ItemComponent;
		p->onMouseDrag = [this]() { if(onMouseDrag) onMouseDrag(this); };
		return std::unique_ptr<juce::Component>(p);
	}
	virtual void paintItem(juce::Graphics& g, int width, int height) override
	{
		juce::LookAndFeel_V4* lf4 = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel());
		if(!lf4) return;
		const juce::LookAndFeel_V4::ColourScheme& cs = lf4->getCurrentColourScheme();
		juce::Rectangle<int> rc(0, 0, width, height);
		bool iscontainer = node->ckinfo.header.isContainer();
		bool isselected = isSelected();
		juce::Colour clrbg = cs.getUIColour(isselected ? juce::LookAndFeel_V4::ColourScheme::highlightedFill : juce::LookAndFeel_V4::ColourScheme::windowBackground);
		juce::Colour clrtxt = cs.getUIColour(isselected ? juce::LookAndFeel_V4::ColourScheme::highlightedText : juce::LookAndFeel_V4::ColourScheme::defaultText);
		g.setColour(clrbg);
		g.fillRect(rc);
		rc.reduce(1, 1);
		// icon
		int cxyi = rc.getHeight();
		juce::Rectangle<int> rci = rc.removeFromLeft(cxyi).reduced(1);
		juce::Image* img = nullptr;
		if(iscontainer)	img = &sharedImages->folderIcon;
		else			img = &sharedImages->fileIcon;
		if(img) g.drawImageWithin(*img, rci.getX(), rci.getY(), rci.getWidth(), rci.getHeight(), juce::RectanglePlacement::doNotResize | juce::RectanglePlacement::centred, false);;
		rc.removeFromLeft(2);
		// text
		g.setColour(clrtxt);
		juce::String s(node->ckinfo.pathElement());
		if(!iscontainer) s += juce::String::formatted(" (%u bytes)", node->ckinfo.header.cksize);
		g.drawText(s, rc, juce::Justification::left, true);
	}
	virtual void itemSelectionChanged(bool) override
	{
		if(onSelectionChanged) onSelectionChanged(this);
	}
};

class RiffNodeTreeView : public juce::TreeView
{
	public:
		virtual bool isInterestedInFileDrag(const juce::StringArray&) override { return false; }
};

// ================================================================================
// MainComponent

class MainComponent : public juce::Component, public juce::MenuBarModel, public juce::ApplicationCommandTarget, public juce::FileDragAndDropTarget
{
private:
	enum CommandIDs
	{
		CommandFileOpen = 1,
		CommandAppExit,
	};
	juce::ApplicationCommandManager applicationCommandManager;
	juce::MenuBarComponent menuBarComponent;
	std::unique_ptr<juce::FileChooser> fileChooser;
	juce::Label infoLabel;
	RiffNodeTreeView treeView;
	juce::Viewport viewport;
	HexViewPane hexViewPane;
	juce::StretchableLayoutManager stretchableLayoutManager;
	class SplitBar : public juce::StretchableLayoutResizerBar
	{
	public:
		SplitBar(juce::StretchableLayoutManager* layoutToUse, int itemIndexInLayout, bool isBarVertical) : juce::StretchableLayoutResizerBar(layoutToUse, itemIndexInLayout, isBarVertical) {}
		virtual void paint(juce::Graphics& g) override
		{
			if(juce::LookAndFeel_V4* lf4 = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
			{
				g.fillAll(lf4->getCurrentColourScheme().getUIColour(juce::LookAndFeel_V4::ColourScheme::UIColour::defaultFill).withAlpha(0.5f));
			}
			juce::StretchableLayoutResizerBar::paint(g);
		}
	};
	SplitBar stretchableLayoutResizerBar;
	RiffDocument riffDocument;
	bool isPerformingFileDragSource = false;
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
public:
	MainComponent() : stretchableLayoutResizerBar(&stretchableLayoutManager, 1, true)
	{
		juce::LookAndFeel_V4* lf4 = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel());
		addAndMakeVisible(menuBarComponent);
		menuBarComponent.setModel(this);
		setApplicationCommandManagerToWatch(&applicationCommandManager);
		applicationCommandManager.registerAllCommandsForTarget(this);
		applicationCommandManager.setFirstCommandTarget(this);
		addKeyListener(applicationCommandManager.getKeyMappings());
		addAndMakeVisible(infoLabel);
		if(lf4)
		{
			infoLabel.setColour(juce::Label::ColourIds::backgroundColourId, lf4->getCurrentColourScheme().getUIColour(juce::LookAndFeel_V4::ColourScheme::defaultFill));
		}
		stretchableLayoutManager.setItemLayout(0, 0, -1, 256);
		stretchableLayoutManager.setItemLayout(1, 8, 8, 8);
		stretchableLayoutManager.setItemLayout(2, 0, -1, 760);
		addAndMakeVisible(treeView);
		treeView.setDefaultOpenness(true);
		treeView.setMultiSelectEnabled(false);
		addAndMakeVisible(viewport);
		viewport.setViewedComponent(&hexViewPane, false);
		hexViewPane.onMouseDrag = [this](const HexViewPane* hvp)
		{
			const riffrw::RiffNode* n = hvp->getRiffNode();
			if(n) performFileDragSource(riffDocument.getContentPath(), n);
		};
		addAndMakeVisible(stretchableLayoutResizerBar);
		setSize(1024, 768);
	}
	virtual ~MainComponent() override
	{
		fileChooser = nullptr;
		clearContent();
	}
	void performFileDragSource(const juce::File& path, const riffrw::RiffNode* n)
	{
		if(isPerformingFileDragSource) return;
		isPerformingFileDragSource = true;
		RiffNodeTempFile::Ptr tmpfile = new RiffNodeTempFile(path, n);
		juce::DragAndDropContainer::performExternalDragDropOfFiles({ tmpfile->getTempPath().getFullPathName() }, false, nullptr, [this, tmpfile]()
		{
			isPerformingFileDragSource = false;
		});
	}
	void clearContent()
	{
		hexViewPane.clearRiffNode();
		treeView.deleteRootItem();
		riffDocument.clearContent();
		infoLabel.setText("", juce::dontSendNotification);
	}
	bool loadContent(const juce::File& path)
	{
		clearContent();
		if(!riffDocument.loadContent(path)) return false;
		const riffrw::RiffNode& nroot = riffDocument.getRootNode();
		juce::TreeViewItem* tvi = generateTree(&nroot);
		treeView.setRootItem(tvi);
		infoLabel.setText(path.getFullPathName(), juce::dontSendNotification);
		return true;
	}
	juce::TreeViewItem* generateTree(const riffrw::RiffNode* n)
	{
		RiffNodeTVItem* tvi = new RiffNodeTVItem(n);
		if(n->ckinfo.header.isContainer())
		{
			for(const auto& ns : n->subnodes)
			{
				tvi->addSubItem(generateTree(&ns));
			}
		}
		else
		{
			tvi->onSelectionChanged = [this](const RiffNodeTVItem* tvi)
			{
				if(tvi->isSelected()) { if(hexViewPane.getRiffNode() != tvi->getRiffNode()) hexViewPane.setRiffNode(riffDocument.getContentPath(), tvi->getRiffNode()); }
				else				  { if(hexViewPane.getRiffNode() == tvi->getRiffNode()) hexViewPane.clearRiffNode(); }
			};
			tvi->onMouseDrag = [this](const RiffNodeTVItem* tvi)
			{
				performFileDragSource(riffDocument.getContentPath(), tvi->getRiffNode());
			};
		}
		return tvi;
	}
	// --------------------------------------------------------------------------------
	// juce::Component
	virtual void resized() override
	{
		juce::Rectangle<int> rc = getLocalBounds();
		menuBarComponent.setBounds(rc.removeFromTop(getLookAndFeel().getDefaultMenuBarHeight()));
		infoLabel.setBounds(rc.removeFromTop((int)infoLabel.getFont().getHeight() + 2));
		juce::Component* vcmp[] = { &treeView, &stretchableLayoutResizerBar, &viewport };
		stretchableLayoutManager.layOutComponents(vcmp, 3, rc.getX(), rc.getY(), rc.getWidth(), rc.getHeight(), false, true);
		hexViewPane.updatePaneSize();
	}
	virtual void paint(juce::Graphics& g) override
	{
		g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
	}
	// --------------------------------------------------------------------------------
	// juce::MenuBarModel
	virtual juce::StringArray getMenuBarNames() override
	{
		return { "File" };
	}
	virtual juce::PopupMenu getMenuForIndex(int imenu, const juce::String&) override
	{
		juce::PopupMenu menu;
		if(imenu == 0)
		{
			menu.addCommandItem(&applicationCommandManager, CommandIDs::CommandFileOpen);
			menu.addSeparator();
			menu.addCommandItem(&applicationCommandManager, CommandIDs::CommandAppExit);
		}
		return menu;
	}
	virtual void menuItemSelected(int, int) override {}
	// --------------------------------------------------------------------------------
	// juce::ApplicationCommandTarget
	virtual juce::ApplicationCommandTarget* getNextCommandTarget() override
	{
		return nullptr;
	}
	virtual void getAllCommands(juce::Array<juce::CommandID>& c) override
	{
		juce::Array<juce::CommandID> commands
		{
			CommandIDs::CommandFileOpen,
			CommandIDs::CommandAppExit,
		};
		c.addArray(commands);
	}
	virtual void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& info) override
	{
		switch(commandID)
		{
			case CommandIDs::CommandFileOpen:
				info.setInfo("Open...", "open RIFF files", "File", 0);
				info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
				break;
			case CommandIDs::CommandAppExit:
				info.setInfo("Exit", "exit", "Application", 0);
				info.addDefaultKeypress(juce::KeyPress::F4Key, juce::ModifierKeys::altModifier);
				return;
		}
	}
	virtual bool perform(const InvocationInfo& info) override
	{
		switch(info.commandID)
		{
			case CommandIDs::CommandFileOpen:
				fileChooser = nullptr;
				fileChooser = std::make_unique<juce::FileChooser>("Open RIFF file");
				fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc)
				{
					juce::File path = fc.getResult();
					if(path != juce::File()) loadContent(fc.getResult());
					fileChooser = nullptr;
				});
				return true;
			case CommandIDs::CommandAppExit:
				juce::JUCEApplication::getInstance()->systemRequestedQuit();
				return true;
		}
		return false;
	}
	// --------------------------------------------------------------------------------
	// juce::FileDragAndDropTarget
	virtual bool isInterestedInFileDrag(const juce::StringArray& files) override
	{
		if(isPerformingFileDragSource) return false;
		return files.size() == 1;
	}
	virtual void filesDropped(const juce::StringArray& files, int, int) override
	{
		if(isPerformingFileDragSource) return;
		if(1 < files.size()) return;
		DBG("filesDropped " << files[0].quoted());
		loadContent(juce::File(files[0]));
	}
};

juce::Component* MainComponentCreateInstance()
{
	return new MainComponent;
}