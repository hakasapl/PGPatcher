#include "GUI/LauncherWindow.hpp"

#include "BethesdaGame.hpp"
#include "GUI/DialogModifiableListCtrl.hpp"
#include "GUI/DialogRecTypeSelector.hpp"
#include "GUI/DialogTextureMapListCtrl.hpp"
#include "ModManagerDirectory.hpp"
#include "PGPatcherGlobals.hpp"
#include "ParallaxGenConfig.hpp"
#include "ParallaxGenPlugin.hpp"

#include <boost/algorithm/string/join.hpp>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/msw/colour.h>
#include <wx/statline.h>
#include <wx/toplevel.h>
#include <wx/wx.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace std;

// Disable owning memory checks because wxWidgets will take care of deleting the objects
// Disable convert member functions to static because these functions need to be non-static for wxWidgets
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// class LauncherWindow
LauncherWindow::LauncherWindow(ParallaxGenConfig& pgc)
    : wxDialog(nullptr, wxID_ANY, "PGPatcher " + string(PG_VERSION) + " Launcher", wxDefaultPosition,
          wxSize(MIN_WIDTH, DEFAULT_HEIGHT), wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX | wxRESIZE_BORDER)
    , m_pgc(pgc)
    , m_gameLocationLocked(false)
{
    // Calculate the scrollbar width (if visible)
    static const int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);

    // Main sizer
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create a horizontal sizer for left and right columns
    auto* columnsSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left/Right sizers
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->SetMinSize(wxSize(LEFTSIZER_MIN_SIZE, -1));
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    //
    // Left Panel
    //

    //
    // Game
    //
    auto* gameSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Game");

    // Game Location
    auto* gameLocationLabel = new wxStaticText(this, wxID_ANY, "Location");
    m_gameLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_gameLocationTextbox->SetToolTip("Path to the game folder (NOT the data folder)");
    m_gameLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onGameLocationChange, this);
    m_gameLocationBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    m_gameLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseGameLocation, this);

    auto* gameLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    gameLocationSizer->Add(m_gameLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    gameLocationSizer->Add(m_gameLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    gameSizer->Add(gameLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    gameSizer->Add(gameLocationSizer, 0, wxEXPAND);

    // Game Type
    auto* gameTypeLabel = new wxStaticText(this, wxID_ANY, "Type");
    gameSizer->Add(gameTypeLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    bool isFirst = true;
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        auto* radio = new wxRadioButton(this, wxID_ANY, BethesdaGame::getStrFromGameType(gameType), wxDefaultPosition,
            wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onGameTypeChange, this);
        isFirst = false;
        m_gameTypeRadios[gameType] = radio;
        gameSizer->Add(radio, 0, wxALL, BORDER_SIZE);
    }

    leftSizer->Add(gameSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Mod Manager
    //
    auto* modManagerSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Conflict Resolution Mod Manager");

    isFirst = true;
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        auto* radio = new wxRadioButton(this, wxID_ANY, ModManagerDirectory::getStrFromModManagerType(mmType),
            wxDefaultPosition, wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        isFirst = false;
        m_modManagerRadios[mmType] = radio;
        modManagerSizer->Add(radio, 0, wxALL, BORDER_SIZE);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onModManagerChange, this);
    }

    leftSizer->Add(modManagerSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // MO2-specific controls (initially hidden)
    m_mo2OptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "MO2 Options");

    auto* mo2InstanceLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* mo2InstanceLocationLabel = new wxStaticText(this, wxID_ANY, "Instance Location");

    m_mo2InstanceLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_mo2InstanceLocationTextbox->SetToolTip(
        "Path to the MO2 instance folder (Folder Icon > Open Instance folder in MO2)");
    m_mo2InstanceLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onMO2InstanceLocationChange, this);

    m_mo2InstanceBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    m_mo2InstanceBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseMO2InstanceLocation, this);

    mo2InstanceLocationSizer->Add(m_mo2InstanceLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    mo2InstanceLocationSizer->Add(m_mo2InstanceBrowseButton, 0, wxALL, BORDER_SIZE);

    // Add the label and dropdown to MO2 options sizer
    m_mo2OptionsSizer->Add(mo2InstanceLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    m_mo2OptionsSizer->Add(mo2InstanceLocationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 0);

    // Add MO2 options to leftSizer but hide it initially
    modManagerSizer->Add(m_mo2OptionsSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Output
    //
    auto* outputSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Output");

    auto* outputLocationLabel = new wxStaticText(this, wxID_ANY,
        "Location recommended to be a mod folder. CANNOT be in your data folder. AVOID DELETING OLD OUTPUT BEFORE "
        "RUNNING "
        "if output is set to a mod folder.");
    outputLocationLabel->Wrap(LEFTSIZER_WRAP_SIZE);
    m_outputLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_outputLocationTextbox->SetToolTip(
        "Path to the output folder - This folder should be used EXCLUSIVELY for PGPatcher");
    m_outputLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onOutputLocationChange, this);

    auto* outputLocationBrowseButton = new wxButton(this, wxID_ANY, "Browse");
    outputLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseOutputLocation, this);

    auto* outputLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    outputLocationSizer->Add(m_outputLocationTextbox, 1, wxEXPAND | wxALL, BORDER_SIZE);
    outputLocationSizer->Add(outputLocationBrowseButton, 0, wxALL, BORDER_SIZE);

    outputSizer->Add(outputLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);
    outputSizer->Add(outputLocationSizer, 0, wxEXPAND);

    m_outputZipCheckbox = new wxCheckBox(this, wxID_ANY, "Zip Output (Keep disabled if outputting to a mod folder)");
    m_outputZipCheckbox->SetToolTip("Zip the output folder after processing");
    m_outputZipCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onOutputZipChange, this);

    outputSizer->Add(m_outputZipCheckbox, 0, wxALL, BORDER_SIZE);

    // Create horizontal sizer for label + combo
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add label
    auto* langLabel = new wxStaticText(this, wxID_ANY, "Plugin Language");
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BORDER_SIZE);

    wxArrayString pluginLangs;
    for (const auto& lang : ParallaxGenPlugin::getAvailablePluginLangStrs()) {
        pluginLangs.Add(lang);
    }
    m_outputPluginLangCombo
        = new wxComboBox(this, wxID_ANY, "Language", wxDefaultPosition, wxDefaultSize, pluginLangs, wxCB_READONLY);
    m_outputPluginLangCombo->Bind(wxEVT_COMBOBOX, &LauncherWindow::onOutputPluginLangChange, this);
    m_outputPluginLangCombo->SetToolTip(
        "Language of embedded strings in output plugin. If a translation for this language is not available for a "
        "record, the default will be used which is usually English.");
    langSizer->Add(m_outputPluginLangCombo, 1, wxEXPAND | wxLEFT, BORDER_SIZE);

    outputSizer->Add(langSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    leftSizer->Add(outputSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Right Panel
    //

    //
    // Pre-Patchers
    //
    auto* prePatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Pre-Patchers");

    m_prePatcherFixMeshLightingCheckbox = new wxCheckBox(this, wxID_ANY, "Fix Mesh Lighting (ENB Only)");
    m_prePatcherFixMeshLightingCheckbox->SetToolTip("Fixes glowing meshes (For ENB users only!)");
    m_prePatcherFixMeshLightingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPrePatcherFixMeshLightingChange, this);
    prePatcherSizer->Add(m_prePatcherFixMeshLightingCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(prePatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Patchers
    //
    auto* shaderPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Shader Patchers");

    m_shaderPatcherParallaxCheckbox = new wxCheckBox(this, wxID_ANY, "Parallax");
    m_shaderPatcherParallaxCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherParallaxChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherParallaxCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherComplexMaterialCheckbox = new wxCheckBox(this, wxID_ANY, "Complex Material");
    m_shaderPatcherComplexMaterialCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherComplexMaterialChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherComplexMaterialCheckbox, 0, wxALL, BORDER_SIZE);

    m_shaderPatcherTruePBRCheckbox = new wxCheckBox(this, wxID_ANY, "TruePBR (CS Only)");
    m_shaderPatcherTruePBRCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherTruePBRCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(shaderPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Shader Transforms
    //
    auto* shaderTransformSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Shader Transforms");

    m_shaderTransformParallaxToCMCheckbox = new wxCheckBox(this, wxID_ANY, "Upgrade Parallax to Complex Material");
    m_shaderTransformParallaxToCMCheckbox->SetToolTip("Upgrades parallax textures and meshes to complex material when "
                                                      "required for compatibility (highly recommended)");
    m_shaderTransformParallaxToCMCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderTransformParallaxToCMChange, this);
    shaderTransformSizer->Add(m_shaderTransformParallaxToCMCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(shaderTransformSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Post-Patchers
    //
    auto* postPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Post-Patchers");

    m_postPatcherRestoreDefaultShadersCheckbox = new wxCheckBox(this, wxID_ANY, "Disable Pre-Patched Materials");
    m_postPatcherRestoreDefaultShadersCheckbox->SetToolTip(
        "Restores shaders to default if parallax or complex "
        "material textures are missing (highly recommended, replaces auto parallax functionality)");
    m_postPatcherRestoreDefaultShadersCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherRestoreDefaultShadersChange, this);
    postPatcherSizer->Add(m_postPatcherRestoreDefaultShadersCheckbox, 0, wxALL, BORDER_SIZE);

    m_postPatcherFixSSSCheckbox = new wxCheckBox(this, wxID_ANY, "Fix Vanilla Subsurface Scattering");
    m_postPatcherFixSSSCheckbox->SetToolTip("Fixes subsurface scattering in meshes, especially foliage");
    m_postPatcherFixSSSCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherFixSSSChange, this);
    postPatcherSizer->Add(m_postPatcherFixSSSCheckbox, 0, wxALL, BORDER_SIZE);

    m_postPatcherHairFlowMapCheckbox = new wxCheckBox(this, wxID_ANY, "Add Hair Flow Map (CS Only)");
    m_postPatcherHairFlowMapCheckbox->SetToolTip(
        "Adds flow maps to texture sets for those that match the normal texture");
    m_postPatcherHairFlowMapCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherHairFlowMapChange, this);
    postPatcherSizer->Add(m_postPatcherHairFlowMapCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(postPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Global Patchers
    //
    auto* globalPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Global Patchers");
    m_globalPatcherFixEffectLightingCSCheckbox
        = new wxCheckBox(this, wxID_ANY, "Fix Effect Lighting (CS Only) (Experimental)");
    m_globalPatcherFixEffectLightingCSCheckbox->SetToolTip("Makes ambient light react to some effect shaders better");
    m_globalPatcherFixEffectLightingCSCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onGlobalPatcherFixEffectLightingCSChange, this);
    globalPatcherSizer->Add(m_globalPatcherFixEffectLightingCSCheckbox, 0, wxALL, BORDER_SIZE);

    rightSizer->Add(globalPatcherSizer, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Processing and RUN buttons
    //

    // Restore defaults button
    auto* restoreDefaultsButton = new wxButton(this, wxID_ANY, "Restore Defaults");
    wxFont restoreDefaultsButtonFont = restoreDefaultsButton->GetFont();
    restoreDefaultsButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    restoreDefaultsButton->SetFont(restoreDefaultsButtonFont);
    restoreDefaultsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onRestoreDefaultsButtonPressed, this);
    rightSizer->Add(restoreDefaultsButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Load config button
    m_loadConfigButton = new wxButton(this, wxID_ANY, "Load Config");
    wxFont loadConfigButtonFont = m_loadConfigButton->GetFont();
    loadConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE);
    m_loadConfigButton->SetFont(loadConfigButtonFont);
    m_loadConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onLoadConfigButtonPressed, this);
    rightSizer->Add(m_loadConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Save config button
    m_saveConfigButton = new wxButton(this, wxID_ANY, "Save Config");
    wxFont saveConfigButtonFont = m_saveConfigButton->GetFont();
    saveConfigButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    m_saveConfigButton->SetFont(saveConfigButtonFont);
    m_saveConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSaveConfigButtonPressed, this);
    rightSizer->Add(m_saveConfigButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Add a horizontal line
    auto* separatorLine = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    rightSizer->Add(separatorLine, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // cancel button on the right side
    auto* cancelButton = new wxButton(this, wxID_CANCEL, "Cancel");
    wxFont cancelButtonFont = cancelButton->GetFont();
    cancelButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    cancelButton->SetFont(cancelButtonFont);
    cancelButton->Bind(wxEVT_BUTTON, &LauncherWindow::onCancelButtonPressed, this);
    rightSizer->Add(cancelButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    // Start Patching button on the right side
    m_okButton = new wxButton(this, wxID_ANY, "Start Patching");
    wxFont okButtonFont = m_okButton->GetFont();
    okButtonFont.SetPointSize(BUTTON_FONT_SIZE); // Set font size to 12
    okButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_okButton->SetFont(okButtonFont);
    m_okButton->SetBackgroundColour(s_OK_BUTTON_COLOR);
    m_okButton->Bind(wxEVT_BUTTON, &LauncherWindow::onOkButtonPressed, this);
    Bind(wxEVT_CLOSE_WINDOW, &LauncherWindow::onClose, this);
    rightSizer->Add(m_okButton, 0, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Processing
    //
    m_processingOptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Processing");

    auto* processingHelpText = new wxStaticText(this, wxID_ANY,
        "These options are used to customize output generation. Avoid changing these unless you know "
        "what you are doing.");
    processingHelpText->Wrap(LEFTSIZER_WRAP_SIZE);
    m_processingOptionsSizer->Add(processingHelpText, 0, wxLEFT | wxRIGHT | wxTOP, BORDER_SIZE);

    auto* processingOptionsHorizontalSizer = new wxBoxSizer(wxHORIZONTAL);

    auto* processingButtonsSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnOpenDialogRecTypeSelector = new wxButton(this, wxID_ANY, "Allowed Record Types");
    btnOpenDialogRecTypeSelector->Bind(wxEVT_BUTTON, &LauncherWindow::onSelectPluginTypesBtn, this);
    processingButtonsSizer->Add(btnOpenDialogRecTypeSelector, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogMeshAllowlist = new wxButton(this, wxID_ANY, "Mesh Allowlist");
    btnOpenDialogMeshAllowlist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesAllowBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshAllowlist, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogMeshBlocklist = new wxButton(this, wxID_ANY, "Mesh Blocklist");
    btnOpenDialogMeshBlocklist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesBlockBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshBlocklist, 0, wxALL | wxEXPAND, BORDER_SIZE);

    auto* btnOpenDialogTextureMaps = new wxButton(this, wxID_ANY, "Texture Rules");
    btnOpenDialogTextureMaps->Bind(wxEVT_BUTTON, &LauncherWindow::onTextureRulesTextureMapsBtn, this);
    processingButtonsSizer->Add(btnOpenDialogTextureMaps, 0, wxALL | wxEXPAND, BORDER_SIZE);

    processingOptionsHorizontalSizer->Add(processingButtonsSizer, 0, wxALL, 0);

    auto* processingCheckboxSizer = new wxBoxSizer(wxVERTICAL);

    m_processingPluginPatchingOptionsESMifyCheckbox = new wxCheckBox(this, wxID_ANY, "ESMify Plugin (Not Recommended)");
    m_processingPluginPatchingOptionsESMifyCheckbox->SetToolTip(
        "ESM flags all the output plugins, not just PGPatcher.esp (don't check this if you don't know what you're "
        "doing)");
    m_processingPluginPatchingOptionsESMifyCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingPluginPatchingOptionsESMifyChange, this);
    processingCheckboxSizer->Add(m_processingPluginPatchingOptionsESMifyCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingMultithreadingCheckbox = new wxCheckBox(this, wxID_ANY, "Multithreading");
    m_processingMultithreadingCheckbox->SetToolTip("Speeds up runtime at the cost of using more resources");
    m_processingMultithreadingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingMultithreadingChange, this);
    processingCheckboxSizer->Add(m_processingMultithreadingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableDevModeCheckbox = new wxCheckBox(this, wxID_ANY, "Enable Mod Dev Mode");
    m_processingEnableDevModeCheckbox->SetToolTip(
        "Enables certain warnings to help those developing mods to work with PGPatcher");
    m_processingEnableDevModeCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDevModeChange, this);
    processingCheckboxSizer->Add(m_processingEnableDevModeCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableDebugLoggingCheckbox = new wxCheckBox(this, wxID_ANY, "Enable Debug Logging");
    m_processingEnableDebugLoggingCheckbox->SetToolTip("Enables debug logging in the output log");
    m_processingEnableDebugLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDebugLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableDebugLoggingCheckbox, 0, wxALL, BORDER_SIZE);

    m_processingEnableTraceLoggingCheckbox = new wxCheckBox(this, wxID_ANY, "Enable Trace Logging");
    m_processingEnableTraceLoggingCheckbox->SetToolTip("Enables trace logging in the output log (very verbose)");
    m_processingEnableTraceLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableTraceLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableTraceLoggingCheckbox, 0, wxALL, BORDER_SIZE);

    processingOptionsHorizontalSizer->Add(processingCheckboxSizer, 0, wxALL, BORDER_SIZE);

    m_processingOptionsSizer->Add(processingOptionsHorizontalSizer, 0, wxALL, 0);

    leftSizer->Add(m_processingOptionsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    //
    // Finalize
    //

    columnsSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 0);
    columnsSizer->Add(rightSizer, 0, wxEXPAND | wxALL, 0);

    mainSizer->Add(columnsSizer, 1, wxEXPAND | wxALL, BORDER_SIZE);

    SetSizerAndFit(mainSizer);
    const auto curSize = GetSize();
    SetSize(MIN_WIDTH, curSize.GetY());
    SetSizeHints(wxSize(MIN_WIDTH, curSize.GetY()), wxSize(-1, curSize.GetY()));

    Bind(wxEVT_INIT_DIALOG, &LauncherWindow::onInitDialog, this);
}

void LauncherWindow::onInitDialog(wxInitDialogEvent& event)
{
    loadConfig();

    // Trigger the updateDeps event to update the dependencies
    updateDisabledElements();
    setGamePathBasedOnExe();

    // Call the base class's event handler if needed
    event.Skip();
}

void LauncherWindow::loadConfig()
{
    // This is where we populate existing params
    const auto initParams = m_pgc.getParams();

    // Game
    if (!m_gameLocationLocked) {
        m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
    }
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (gameType == initParams.Game.type) {
            m_gameTypeRadios[gameType]->SetValue(true);
        }
    }

    // Mod Manager
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        if (mmType == initParams.ModManager.type) {
            m_modManagerRadios[mmType]->SetValue(true);

            // Show MO2 options only if MO2 is selected
            if (mmType == ModManagerDirectory::ModManagerType::MODORGANIZER2) {
                m_mo2InstanceLocationTextbox->Enable(true);
                m_mo2InstanceBrowseButton->Enable(true);
            } else {
                m_mo2InstanceLocationTextbox->Enable(false);
                m_mo2InstanceBrowseButton->Enable(false);
            }
        }
    }

    // MO2-specific options
    m_mo2InstanceLocationTextbox->SetValue(initParams.ModManager.mo2InstanceDir.wstring());

    // Manually trigger the onMO2InstanceLocationChange to populate the listbox
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly

    // Output
    m_outputLocationTextbox->SetValue(initParams.Output.dir.wstring());
    m_outputZipCheckbox->SetValue(initParams.Output.zip);
    m_outputPluginLangCombo->SetStringSelection(
        ParallaxGenPlugin::getStringFromPluginLang(initParams.Output.pluginLang));

    // Processing
    m_processingPluginPatchingOptionsESMifyCheckbox->SetValue(initParams.Processing.pluginESMify);
    m_processingMultithreadingCheckbox->SetValue(initParams.Processing.multithread);
    m_processingEnableDevModeCheckbox->SetValue(initParams.Processing.enableModDevMode);
    m_processingEnableDebugLoggingCheckbox->SetValue(initParams.Processing.enableDebugLogging);
    m_processingEnableTraceLoggingCheckbox->SetValue(initParams.Processing.enableTraceLogging);
    m_meshRulesAllowListState = initParams.Processing.allowList;
    m_meshRulesBlockListState = initParams.Processing.blockList;
    m_textureRulesTextureMapsState = initParams.Processing.textureMaps;
    m_DialogRecTypeSelectorState = initParams.Processing.allowedModelRecordTypes;

    // Pre-Patchers
    m_prePatcherFixMeshLightingCheckbox->SetValue(initParams.PrePatcher.fixMeshLighting);

    // Shader Patchers
    m_shaderPatcherParallaxCheckbox->SetValue(initParams.ShaderPatcher.parallax);
    m_shaderPatcherComplexMaterialCheckbox->SetValue(initParams.ShaderPatcher.complexMaterial);
    m_shaderPatcherTruePBRCheckbox->SetValue(initParams.ShaderPatcher.truePBR);

    // Shader Transforms
    m_shaderTransformParallaxToCMCheckbox->SetValue(initParams.ShaderTransforms.parallaxToCM);

    // Post-Patchers
    m_postPatcherRestoreDefaultShadersCheckbox->SetValue(initParams.PostPatcher.disablePrePatchedMaterials);
    m_postPatcherFixSSSCheckbox->SetValue(initParams.PostPatcher.fixSSS);
    m_postPatcherHairFlowMapCheckbox->SetValue(initParams.PostPatcher.hairFlowMap);

    // Global Patchers
    m_globalPatcherFixEffectLightingCSCheckbox->SetValue(initParams.GlobalPatcher.fixEffectLightingCS);
}

// Component event handlers

void LauncherWindow::onGameLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onGameTypeChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_gameLocationLocked) {
        updateDisabledElements();
        return;
    }

    const auto initParams = m_pgc.getParams();

    // update the game location textbox from bethesdagame
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios[gameType]->GetValue()) {
            if (initParams.Game.type == gameType) {
                m_gameLocationTextbox->SetValue(initParams.Game.dir.wstring());
            } else {
                m_gameLocationTextbox->SetValue(BethesdaGame::findGamePathFromSteam(gameType).wstring());
            }

            setGamePathBasedOnExe();
            return;
        }
    }

    updateDisabledElements();
}

void LauncherWindow::onModManagerChange([[maybe_unused]] wxCommandEvent& event)
{
    // Show MO2 options only if the MO2 radio button is selected
    const bool isMO2Selected
        = (event.GetEventObject() == m_modManagerRadios[ModManagerDirectory::ModManagerType::MODORGANIZER2]);
    m_mo2InstanceLocationTextbox->Enable(isMO2Selected);
    m_mo2InstanceBrowseButton->Enable(isMO2Selected);

    updateMO2Items();

    Layout(); // Refresh layout to apply visibility changes
    Fit();

    updateDisabledElements();
    setGamePathBasedOnExe();
}

void LauncherWindow::onOutputLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onOutputZipChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onProcessingPluginPatchingOptionsESMifyChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onOutputPluginLangChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onProcessingMultithreadingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableDevModeChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableDebugLoggingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onProcessingEnableTraceLoggingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPrePatcherFixMeshLightingChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onGlobalPatcherFixEffectLightingCSChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherParallaxChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onShaderPatcherComplexMaterialChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onShaderPatcherTruePBRChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onShaderTransformParallaxToCMChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPostPatcherRestoreDefaultShadersChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onPostPatcherFixSSSChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onPostPatcherHairFlowMapChange([[maybe_unused]] wxCommandEvent& event)
{
    updateDisabledElements();
}

void LauncherWindow::onMeshRulesAllowBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogModifiableListCtrl dialog(this, "Mesh Rules Allowlist",
        "If any rules exist here, only meshes matching them will be patched. Enter path to mesh like "
        "\"meshes/armor/helmet.nif\" or use wildcards (* is the wildcard) to allowlist entire "
        "folders/files.");
    dialog.populateList(m_meshRulesAllowListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesAllowListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onMeshRulesBlockBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogModifiableListCtrl dialog(this, "Mesh Rules Blocklist",
        "Any meshes matching rules here will not be patched. Enter path to mesh like \"meshes/armor/helmet.nif\" or "
        "use wildcards (* is the wildcard) to blocklist entire "
        "folders/files.");
    dialog.populateList(m_meshRulesBlockListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesBlockListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onTextureRulesTextureMapsBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogTextureMapListCtrl dialog(this, "Texture Rules",
        "Use this to tell PGPatcher what type of texture something is if the auto detection is wrong (very rare). "
        "Enter the full path to the texture like \"textures/armor/helmet.dds\" and select the type of texture. "
        "Wilcards are NOT supported here. A texture can be ignored by setting it to \"unknown\"");
    dialog.populateList(m_textureRulesTextureMapsState);
    if (dialog.ShowModal() == wxID_OK) {
        m_textureRulesTextureMapsState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onSelectPluginTypesBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogRecTypeSelector selectorDialog(this);
    selectorDialog.populateList(m_DialogRecTypeSelectorState);
    if (selectorDialog.ShowModal() == wxID_OK) {
        m_DialogRecTypeSelectorState = selectorDialog.getSelectedRecordTypes();
        updateDisabledElements();
    }
}

void LauncherWindow::getParams(ParallaxGenConfig::PGParams& params) const
{
    // Game
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios.at(gameType)->GetValue()) {
            params.Game.type = gameType;
            break;
        }
    }
    params.Game.dir = m_gameLocationTextbox->GetValue().ToStdWstring();

    // Mod Manager
    for (const auto& mmType : ModManagerDirectory::getModManagerTypes()) {
        if (m_modManagerRadios.at(mmType)->GetValue()) {
            params.ModManager.type = mmType;
            break;
        }
    }
    params.ModManager.mo2InstanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Output
    params.Output.dir = m_outputLocationTextbox->GetValue().ToStdWstring();
    params.Output.zip = m_outputZipCheckbox->GetValue();
    params.Output.pluginLang
        = ParallaxGenPlugin::getPluginLangFromString(m_outputPluginLangCombo->GetStringSelection().ToStdString());

    // Processing
    params.Processing.pluginESMify = m_processingPluginPatchingOptionsESMifyCheckbox->GetValue();
    params.Processing.multithread = m_processingMultithreadingCheckbox->GetValue();
    params.Processing.enableModDevMode = m_processingEnableDevModeCheckbox->GetValue();
    params.Processing.enableDebugLogging = m_processingEnableDebugLoggingCheckbox->GetValue();
    params.Processing.enableTraceLogging = m_processingEnableTraceLoggingCheckbox->GetValue();
    params.Processing.allowList = m_meshRulesAllowListState;
    params.Processing.blockList = m_meshRulesBlockListState;
    params.Processing.textureMaps = m_textureRulesTextureMapsState;
    params.Processing.allowedModelRecordTypes = m_DialogRecTypeSelectorState;

    // Pre-Patchers
    params.PrePatcher.fixMeshLighting = m_prePatcherFixMeshLightingCheckbox->GetValue();

    // Shader Patchers
    params.ShaderPatcher.parallax = m_shaderPatcherParallaxCheckbox->GetValue();
    params.ShaderPatcher.complexMaterial = m_shaderPatcherComplexMaterialCheckbox->GetValue();
    params.ShaderPatcher.truePBR = m_shaderPatcherTruePBRCheckbox->GetValue();

    // Shader Transforms
    params.ShaderTransforms.parallaxToCM = m_shaderTransformParallaxToCMCheckbox->GetValue();

    // Post-Patchers
    params.PostPatcher.disablePrePatchedMaterials = m_postPatcherRestoreDefaultShadersCheckbox->GetValue();
    params.PostPatcher.fixSSS = m_postPatcherFixSSSCheckbox->GetValue();
    params.PostPatcher.hairFlowMap = m_postPatcherHairFlowMapCheckbox->GetValue();

    // Global Patchers
    params.GlobalPatcher.fixEffectLightingCS = m_globalPatcherFixEffectLightingCSCheckbox->GetValue();
}

void LauncherWindow::onBrowseGameLocation([[maybe_unused]] wxCommandEvent& event)
{
    if (m_gameLocationLocked) {
        return;
    }

    wxDirDialog dialog(this, "Select Game Location", m_gameLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_gameLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::onBrowseMO2InstanceLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this, "Select MO2 Instance Location", m_mo2InstanceLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_mo2InstanceLocationTextbox->SetValue(dialog.GetPath());
    }

    // Trigger the change event to update the profiles
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly
}

void LauncherWindow::updateMO2Items()
{
    // check if MO2 is selected
    if (!m_modManagerRadios[ModManagerDirectory::ModManagerType::MODORGANIZER2]->GetValue()) {
        // If MO2 is not selected, clear the instance location textbox and return
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        m_gameLocationLocked = false;
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(true);
        }
        return;
    }

    const auto instanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Check if modorganizer.ini exists
    if (!ModManagerDirectory::isValidMO2InstanceDir(instanceDir)) {
        // set instance directory text to red
        m_mo2InstanceLocationTextbox->SetForegroundColour(*wxRED);
        return;
    }

    // set instance directory text to black
    m_mo2InstanceLocationTextbox->SetForegroundColour(*wxBLACK);

    // Get game path
    const auto gamePathMO2 = ModManagerDirectory::getGamePathFromInstanceDir(instanceDir);
    if (!gamePathMO2.empty()) {
        // found the game path, set it to the game location textbox
        m_gameLocationTextbox->SetValue(gamePathMO2.wstring());
        // disable the game location textbox
        m_gameLocationTextbox->Enable(false);
        m_gameLocationBrowseButton->Enable(false);
        m_gameLocationLocked = true;
    } else {
        // not found, enable the game location textbox
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        m_gameLocationLocked = false;
    }

    // Get game type
    const auto gameTypeMO2 = ModManagerDirectory::getGameTypeFromInstanceDir(instanceDir);
    if (gameTypeMO2 != BethesdaGame::GameType::UNKNOWN) {
        m_gameTypeRadios[gameTypeMO2]->SetValue(true);
        // disable all radio buttons
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(false);
        }
    } else {
        // enable all radio buttons
        for (const auto& gameType : BethesdaGame::getGameTypes()) {
            m_gameTypeRadios[gameType]->Enable(true);
        }
    }
}

void LauncherWindow::onMO2InstanceLocationChange([[maybe_unused]] wxCommandEvent& event) { updateMO2Items(); }

void LauncherWindow::onBrowseOutputLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this, "Select Output Location", m_outputLocationTextbox->GetValue());
    if (dialog.ShowModal() == wxID_OK) {
        m_outputLocationTextbox->SetValue(dialog.GetPath());
    }
}

void LauncherWindow::updateDisabledElements()
{
    ParallaxGenConfig::PGParams curParams = m_pgc.getParams();
    getParams(curParams);

    // Upgrade parallax to CM rules
    if (curParams.ShaderTransforms.parallaxToCM) {
        // disable and check vanilla parallax patcher
        m_shaderPatcherParallaxCheckbox->SetValue(true);
        m_shaderPatcherParallaxCheckbox->Enable(false);

        // disable and check CM patcher
        m_shaderPatcherComplexMaterialCheckbox->SetValue(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(false);
    } else {
        m_shaderPatcherParallaxCheckbox->Enable(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(true);
    }

    // save button
    m_saveConfigButton->Enable(curParams != m_pgc.getParams());

    // logging checkboxes
    if (curParams.Processing.enableDebugLogging) {
        m_processingEnableTraceLoggingCheckbox->Enable(true);
    } else {
        m_processingEnableTraceLoggingCheckbox->SetValue(false);
        m_processingEnableTraceLoggingCheckbox->Enable(false);
    }
}

void LauncherWindow::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // All validation passed, proceed with OK actions
        EndModal(wxID_OK);
    }
}

void LauncherWindow::onCancelButtonPressed([[maybe_unused]] wxCommandEvent& event) { wxTheApp->Exit(); }

void LauncherWindow::onSaveConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // Disable button
        updateDisabledElements();
    }
}

void LauncherWindow::onLoadConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    const int response
        = wxMessageBox("Are you sure you want to load the config from the file? This action will overwrite all "
                       "current unsaved settings.",
            "Confirm Load Config", wxYES_NO | wxICON_WARNING, this);

    if (response != wxYES) {
        return;
    }

    // Load the config from the file
    loadConfig();

    updateDisabledElements();
}

void LauncherWindow::onRestoreDefaultsButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    // Show a confirmation dialog
    const int response
        = wxMessageBox("Are you sure you want to restore the default settings? This action cannot be undone.",
            "Confirm Restore Defaults", wxYES_NO | wxICON_WARNING, this);

    if (response != wxYES) {
        return;
    }

    // Reset the config to the default
    m_pgc.setParams(ParallaxGenConfig::getDefaultParams());

    loadConfig();

    updateDisabledElements();
}

auto LauncherWindow::saveConfig() -> bool
{
    vector<string> errors;
    ParallaxGenConfig::PGParams params = m_pgc.getParams();
    getParams(params);

    // Validate the parameters
    if (!ParallaxGenConfig::validateParams(params, errors)) {
        wxMessageDialog errorDialog(this, boost::algorithm::join(errors, "\n"), "Errors", wxOK | wxICON_ERROR);
        errorDialog.ShowModal();
        return false;
    }

    m_pgc.setParams(params);
    m_pgc.saveUserConfig();
    return true;
}

void LauncherWindow::onClose([[maybe_unused]] wxCloseEvent& event) { wxTheApp->Exit(); }

void LauncherWindow::setGamePathBasedOnExe()
{
    const auto exePath = PGPatcherGlobals::getEXEPath();
    if (exePath.empty()) {
        return;
    }

    auto curParams = m_pgc.getParams();
    getParams(curParams);
    const auto curGameType = curParams.Game.type;

    const auto curModManagerType = curParams.ModManager.type;
    if (curModManagerType == ModManagerDirectory::ModManagerType::MODORGANIZER2) {
        // don't change the game path if MO2 is selected since that's enforced by MO2 instead
        return;
    }

    const auto gamePath = exePath.parent_path().parent_path();
    if (BethesdaGame::isGamePathValid(gamePath, curGameType)) {
        m_gameLocationTextbox->SetValue(gamePath.wstring());

        // disable textbox and browse button
        m_gameLocationTextbox->Enable(false);
        m_gameLocationBrowseButton->Enable(false);
        m_gameLocationLocked = true;
    } else {
        // enable textbox and browse button
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        m_gameLocationLocked = false;
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
