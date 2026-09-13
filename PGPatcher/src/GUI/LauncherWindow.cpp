#include "GUI/LauncherWindow.hpp"

#include "GUI/DialogModifiableListCtrl.hpp"
#include "GUI/DialogRecTypeSelector.hpp"
#include "GUI/DialogSettings.hpp"
#include "GUI/DialogTextureMapListCtrl.hpp"
#include "GUI/PGMessageBox.hpp"
#include "PGConfig.hpp"
#include "PGLocale.hpp"
#include "PGModManager.hpp"
#include "PGPatcherGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "PGUI.hpp"
#include "common/BethesdaGame.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/msw/colour.h>
#include <wx/statline.h>
#include <wx/toplevel.h>
#include <wx/wx.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Disable owning memory checks because wxWidgets will take care of deleting the objects.
// Disable convert member functions to static because these functions need to be non-static for wxWidgets.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)

// Class LauncherWindow.
LauncherWindow::LauncherWindow(PGConfig& pgc,
                               std::optional<PGConfig::PGParams> initialParams)
    : wxDialog(nullptr,
               wxID_ANY,
               wxString::Format(pgTr("launcher.title"),
                                PG_FULL_VERSION),
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX | wxRESIZE_BORDER)
    , m_pgc(pgc)
    , m_initialParams(std::move(initialParams))

{
    SetIcons(PGUI::getAppIcons());

    // Calculate the scrollbar width (if visible).
    static const int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);

    // Pixel sizes are defined for 100% scaling, so scale them to the DPI of the monitor showing the launcher.
    const int borderSize = FromDIP(borderSizeDIP);

    // Main sizer.
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Create a horizontal sizer for left and right columns.
    auto* columnsSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left/Right sizers.
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->SetMinSize(wxSize(FromDIP(leftSizerMinSize), -1));
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);

    //
    // Left Panel.
    //

    //
    // Game.
    //
    auto* gameSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.game.title"));

    // Game Location.
    auto* gameLocationLabel = new wxStaticText(this, wxID_ANY, pgTr("launcher.game.location.label"));
    m_gameLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_gameLocationTextbox->SetToolTip(pgTr("launcher.game.location.tooltip"));
    m_gameLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onGameLocationChange, this);
    m_gameLocationBrowseButton = new wxButton(this, wxID_ANY, pgTr("common.browse"));
    m_gameLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseGameLocation, this);

    auto* gameLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    gameLocationSizer->Add(m_gameLocationTextbox, 1, wxEXPAND | wxALL, borderSize);
    gameLocationSizer->Add(m_gameLocationBrowseButton, 0, wxALL, borderSize);

    gameSizer->Add(gameLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, borderSize);
    gameSizer->Add(gameLocationSizer, 0, wxEXPAND);

    // Game Type.
    auto* gameTypeLabel = new wxStaticText(this, wxID_ANY, pgTr("launcher.game.type.label"));
    gameSizer->Add(gameTypeLabel, 0, wxLEFT | wxRIGHT | wxTOP, borderSize);

    bool isFirst = true;
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        auto* radio = new wxRadioButton(this,
                                        wxID_ANY,
                                        BethesdaGame::getStrFromGameType(gameType),
                                        wxDefaultPosition,
                                        wxDefaultSize,
                                        isFirst ? wxRB_GROUP : 0);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onGameTypeChange, this);
        isFirst = false;
        m_gameTypeRadios[gameType] = radio;
        gameSizer->Add(radio, 0, wxALL, borderSize);
    }

    leftSizer->Add(gameSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Mod Manager.
    //
    auto* modManagerSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.modManager.title"));

    isFirst = true;
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        auto mmString = wxString(PGModManager::getStrFromModManagerType(mmType));
        if (mmType == PGModManager::ModManagerType::None)
            mmString += pgTr("launcher.modManager.noneSuffix");

        auto* radio
            = new wxRadioButton(this, wxID_ANY, mmString, wxDefaultPosition, wxDefaultSize, isFirst ? wxRB_GROUP : 0);
        isFirst = false;
        m_modManagerRadios[mmType] = radio;
        modManagerSizer->Add(radio, 0, wxALL, borderSize);
        radio->Bind(wxEVT_RADIOBUTTON, &LauncherWindow::onModManagerChange, this);
    }

    leftSizer->Add(modManagerSizer, 0, wxEXPAND | wxALL, borderSize);

    // MO2-specific controls (initially hidden).
    m_mo2OptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.mo2Options.title"));

    auto* mo2InstanceLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* mo2InstanceLocationLabel
        = new wxStaticText(this, wxID_ANY, pgTr("launcher.mo2Options.instanceLocation.label"));

    m_mo2InstanceLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_mo2InstanceLocationTextbox->SetToolTip(pgTr("launcher.mo2Options.instanceLocation.tooltip"));
    m_mo2InstanceLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onMO2InstanceLocationChange, this);

    m_mo2InstanceBrowseButton = new wxButton(this, wxID_ANY, pgTr("common.browse"));
    m_mo2InstanceBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseMO2InstanceLocation, this);

    mo2InstanceLocationSizer->Add(m_mo2InstanceLocationTextbox, 1, wxEXPAND | wxALL, borderSize);
    mo2InstanceLocationSizer->Add(m_mo2InstanceBrowseButton, 0, wxALL, borderSize);

    // Add the label and dropdown to MO2 options sizer.
    m_mo2OptionsSizer->Add(mo2InstanceLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, borderSize);
    m_mo2OptionsSizer->Add(mo2InstanceLocationSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 0);

    // Add MO2 options to leftSizer but hide it initially.
    modManagerSizer->Add(m_mo2OptionsSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Output.
    //
    auto* outputSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.output.title"));

    auto* outputLocationLabel = new wxStaticText(this, wxID_ANY, pgTr("launcher.output.location.help"));
    outputLocationLabel->Wrap(FromDIP(leftSizerWrapSize));
    m_outputLocationTextbox = new wxTextCtrl(this, wxID_ANY);
    m_outputLocationTextbox->SetToolTip(pgTr("launcher.output.location.tooltip"));
    m_outputLocationTextbox->Bind(wxEVT_TEXT, &LauncherWindow::onOutputLocationChange, this);

    auto* outputLocationBrowseButton = new wxButton(this, wxID_ANY, pgTr("common.browse"));
    outputLocationBrowseButton->Bind(wxEVT_BUTTON, &LauncherWindow::onBrowseOutputLocation, this);

    auto* outputLocationSizer = new wxBoxSizer(wxHORIZONTAL);
    outputLocationSizer->Add(m_outputLocationTextbox, 1, wxEXPAND | wxALL, borderSize);
    outputLocationSizer->Add(outputLocationBrowseButton, 0, wxALL, borderSize);

    outputSizer->Add(outputLocationLabel, 0, wxLEFT | wxRIGHT | wxTOP, borderSize);
    outputSizer->Add(outputLocationSizer, 0, wxEXPAND);

    m_outputZipCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.output.zip.label"));
    m_outputZipCheckbox->SetToolTip(pgTr("launcher.output.zip.tooltip"));
    m_outputZipCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onOutputZipChange, this);

    outputSizer->Add(m_outputZipCheckbox, 0, wxALL, borderSize);

    // Create horizontal sizer for label + combo.
    auto* langSizer = new wxBoxSizer(wxHORIZONTAL);

    // Add label.
    auto* langLabel = new wxStaticText(this, wxID_ANY, pgTr("launcher.output.pluginLang.label"));
    langSizer->Add(langLabel, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, borderSize);

    wxArrayString pluginLangs;
    for (const auto& lang : PGPlugin::getAvailablePluginLangStrs())
        pluginLangs.Add(lang);
    m_outputPluginLangCombo = new wxComboBox(this,
                                             wxID_ANY,
                                             pgTr("launcher.output.pluginLang.placeholder"),
                                             wxDefaultPosition,
                                             wxDefaultSize,
                                             pluginLangs,
                                             wxCB_READONLY);
    m_outputPluginLangCombo->Bind(wxEVT_COMBOBOX, &LauncherWindow::onOutputPluginLangChange, this);
    m_outputPluginLangCombo->SetToolTip(pgTr("launcher.output.pluginLang.tooltip"));
    langSizer->Add(m_outputPluginLangCombo, 1, wxEXPAND | wxLEFT, borderSize);

    outputSizer->Add(langSizer, 0, wxEXPAND | wxALL, borderSize);

    leftSizer->Add(outputSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Right Panel.
    //

    //
    // Pre-Patchers.
    //
    auto* prePatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.prePatchers.title"));

    m_prePatcherFixMeshLightingCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.prePatchers.fixMeshLighting.label"));
    m_prePatcherFixMeshLightingCheckbox->SetToolTip(pgTr("launcher.prePatchers.fixMeshLighting.tooltip"));
    m_prePatcherFixMeshLightingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPrePatcherFixMeshLightingChange, this);
    prePatcherSizer->Add(m_prePatcherFixMeshLightingCheckbox, 0, wxALL, borderSize);

    rightSizer->Add(prePatcherSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Shader Patchers.
    //
    auto* shaderPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.shaderPatchers.title"));

    m_shaderPatcherParallaxCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.shaderPatchers.parallax.label"));
    m_shaderPatcherParallaxCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherParallaxChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherParallaxCheckbox, 0, wxALL, borderSize);

    m_shaderPatcherComplexMaterialCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.shaderPatchers.complexMaterial.label"));
    m_shaderPatcherComplexMaterialCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherComplexMaterialChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherComplexMaterialCheckbox, 0, wxALL, borderSize);

    m_shaderPatcherTruePBRCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.shaderPatchers.truePBR.label"));
    m_shaderPatcherTruePBRCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onShaderPatcherTruePBRChange, this);
    shaderPatcherSizer->Add(m_shaderPatcherTruePBRCheckbox, 0, wxALL, borderSize);

    rightSizer->Add(shaderPatcherSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Shader Transforms.
    //
    auto* shaderTransformSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.shaderTransforms.title"));

    m_shaderTransformParallaxToCMCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.shaderTransforms.parallaxToCM.label"));
    m_shaderTransformParallaxToCMCheckbox->SetToolTip(pgTr("launcher.shaderTransforms.parallaxToCM.tooltip"));
    m_shaderTransformParallaxToCMCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onShaderTransformParallaxToCMChange, this);
    shaderTransformSizer->Add(m_shaderTransformParallaxToCMCheckbox, 0, wxALL, borderSize);

    rightSizer->Add(shaderTransformSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Post-Patchers.
    //
    auto* postPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.postPatchers.title"));

    m_postPatcherRestoreDefaultShadersCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.postPatchers.disablePrePatchedMaterials.label"));
    m_postPatcherRestoreDefaultShadersCheckbox->SetToolTip(
        pgTr("launcher.postPatchers.disablePrePatchedMaterials.tooltip"));
    m_postPatcherRestoreDefaultShadersCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherRestoreDefaultShadersChange, this);
    postPatcherSizer->Add(m_postPatcherRestoreDefaultShadersCheckbox, 0, wxALL, borderSize);

    m_postPatcherFixSSSCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.postPatchers.fixSSS.label"));
    m_postPatcherFixSSSCheckbox->SetToolTip(pgTr("launcher.postPatchers.fixSSS.tooltip"));
    m_postPatcherFixSSSCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherFixSSSChange, this);
    postPatcherSizer->Add(m_postPatcherFixSSSCheckbox, 0, wxALL, borderSize);

    m_postPatcherHairFlowMapCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.postPatchers.hairFlowMap.label"));
    m_postPatcherHairFlowMapCheckbox->SetToolTip(pgTr("launcher.postPatchers.hairFlowMap.tooltip"));
    m_postPatcherHairFlowMapCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onPostPatcherHairFlowMapChange, this);
    postPatcherSizer->Add(m_postPatcherHairFlowMapCheckbox, 0, wxALL, borderSize);

    rightSizer->Add(postPatcherSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Global Patchers.
    //
    // auto* globalPatcherSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Global Patchers");
    // rightSizer->Add(globalPatcherSizer, 0, wxEXPAND | wxALL, borderSize);

    //
    // Processing and RUN buttons.
    //

    // Restore defaults and load config buttons: default (smaller) font, side by side in one row above the save config.
    // Button.
    auto* restoreDefaultsButton = new wxButton(this, wxID_ANY, pgTr("launcher.buttons.restoreDefaults"));
    restoreDefaultsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onRestoreDefaultsButtonPressed, this);

    m_loadConfigButton = new wxButton(this, wxID_ANY, pgTr("launcher.buttons.loadConfig"));
    m_loadConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onLoadConfigButtonPressed, this);

    // A grid sizer gives both buttons the same width.
    auto* configButtonsSizer = new wxGridSizer(1, 2, 0, borderSize);
    configButtonsSizer->Add(restoreDefaultsButton, 0, wxEXPAND);
    configButtonsSizer->Add(m_loadConfigButton, 0, wxEXPAND);
    rightSizer->Add(configButtonsSizer, 0, wxEXPAND | wxALL, borderSize);

    // Save config button.
    m_saveConfigButton = new wxButton(this, wxID_ANY, pgTr("launcher.buttons.saveConfig"));
    wxFont saveConfigButtonFont = m_saveConfigButton->GetFont();
    saveConfigButtonFont.SetPointSize(buttonFontSize); // Set font size to 12
    m_saveConfigButton->SetFont(saveConfigButtonFont);
    m_saveConfigButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSaveConfigButtonPressed, this);
    rightSizer->Add(m_saveConfigButton, 0, wxEXPAND | wxALL, borderSize);

    // Add a horizontal line.
    auto* separatorLine = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    rightSizer->Add(separatorLine, 0, wxEXPAND | wxALL, borderSize);

    // Cancel button on the right side.
    auto* cancelButton = new wxButton(this, wxID_CANCEL, pgTr("common.cancel"));
    wxFont cancelButtonFont = cancelButton->GetFont();
    cancelButtonFont.SetPointSize(buttonFontSize); // Set font size to 12
    cancelButton->SetFont(cancelButtonFont);
    cancelButton->Bind(wxEVT_BUTTON, &LauncherWindow::onCancelButtonPressed, this);
    rightSizer->Add(cancelButton, 0, wxEXPAND | wxALL, borderSize);

    // Start Patching button on the right side.
    m_okButton = new wxButton(this, wxID_ANY, pgTr("launcher.buttons.startPatching"));
    wxFont okButtonFont = m_okButton->GetFont();
    okButtonFont.SetPointSize(buttonFontSize); // Set font size to 12
    okButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_okButton->SetFont(okButtonFont);
    m_okButton->SetToolTip(pgTr("launcher.buttons.startPatchingTooltip"));
    m_okButton->Bind(wxEVT_BUTTON, &LauncherWindow::onOkButtonPressed, this);
    Bind(wxEVT_CLOSE_WINDOW, &LauncherWindow::onClose, this);
    rightSizer->Add(m_okButton, 0, wxEXPAND | wxALL, borderSize);

    // Update Output button below it (only enabled when the output location holds a previous output).
    m_updateOutputButton = new wxButton(this, wxID_ANY, pgTr("launcher.buttons.updateOutput"));
    wxFont updateOutputButtonFont = m_updateOutputButton->GetFont();
    updateOutputButtonFont.SetPointSize(buttonFontSize);
    m_updateOutputButton->SetFont(updateOutputButtonFont);
    m_updateOutputButton->SetToolTip(pgTr("launcher.buttons.updateOutputTooltip"));
    m_updateOutputButton->Bind(wxEVT_BUTTON, &LauncherWindow::onUpdateOutputButtonPressed, this);
    rightSizer->Add(m_updateOutputButton, 0, wxEXPAND | wxALL, borderSize);

    //
    // Processing.
    //
    m_processingOptionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, pgTr("launcher.processing.title"));

    auto* processingHelpText = new wxStaticText(this, wxID_ANY, pgTr("launcher.processing.help"));
    processingHelpText->Wrap(FromDIP(leftSizerWrapSize));
    m_processingOptionsSizer->Add(processingHelpText, 0, wxLEFT | wxRIGHT | wxTOP, borderSize);

    auto* processingOptionsHorizontalSizer = new wxBoxSizer(wxHORIZONTAL);

    auto* processingButtonsSizer = new wxBoxSizer(wxVERTICAL);

    auto* btnOpenDialogRecTypeSelector = new wxButton(this, wxID_ANY, pgTr("launcher.processing.allowedRecordTypes"));
    btnOpenDialogRecTypeSelector->Bind(wxEVT_BUTTON, &LauncherWindow::onSelectPluginTypesBtn, this);
    processingButtonsSizer->Add(btnOpenDialogRecTypeSelector, 0, wxALL | wxEXPAND, borderSize);

    auto* btnOpenDialogMeshAllowlist = new wxButton(this, wxID_ANY, pgTr("launcher.processing.meshAllowlist"));
    btnOpenDialogMeshAllowlist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesAllowBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshAllowlist, 0, wxALL | wxEXPAND, borderSize);

    auto* btnOpenDialogMeshBlocklist = new wxButton(this, wxID_ANY, pgTr("launcher.processing.meshBlocklist"));
    btnOpenDialogMeshBlocklist->Bind(wxEVT_BUTTON, &LauncherWindow::onMeshRulesBlockBtn, this);
    processingButtonsSizer->Add(btnOpenDialogMeshBlocklist, 0, wxALL | wxEXPAND, borderSize);

    auto* btnOpenDialogTextureMaps = new wxButton(this, wxID_ANY, pgTr("launcher.processing.textureRules"));
    btnOpenDialogTextureMaps->Bind(wxEVT_BUTTON, &LauncherWindow::onTextureRulesTextureMapsBtn, this);
    processingButtonsSizer->Add(btnOpenDialogTextureMaps, 0, wxALL | wxEXPAND, borderSize);

    processingOptionsHorizontalSizer->Add(processingButtonsSizer, 0, wxALL, 0);

    auto* processingCheckboxSizer = new wxBoxSizer(wxVERTICAL);

    m_processingMultithreadingCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.processing.multithreading.label"));
    m_processingMultithreadingCheckbox->SetToolTip(pgTr("launcher.processing.multithreading.tooltip"));
    m_processingMultithreadingCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingMultithreadingChange, this);
    processingCheckboxSizer->Add(m_processingMultithreadingCheckbox, 0, wxALL, borderSize);

    m_processingEnableDevModeCheckbox = new wxCheckBox(this, wxID_ANY, pgTr("launcher.processing.devMode.label"));
    m_processingEnableDevModeCheckbox->SetToolTip(pgTr("launcher.processing.devMode.tooltip"));
    m_processingEnableDevModeCheckbox->Bind(wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDevModeChange, this);
    processingCheckboxSizer->Add(m_processingEnableDevModeCheckbox, 0, wxALL, borderSize);

    m_processingEnableDebugLoggingCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.processing.debugLogging.label"));
    m_processingEnableDebugLoggingCheckbox->SetToolTip(pgTr("launcher.processing.debugLogging.tooltip"));
    m_processingEnableDebugLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableDebugLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableDebugLoggingCheckbox, 0, wxALL, borderSize);

    m_processingEnableTraceLoggingCheckbox
        = new wxCheckBox(this, wxID_ANY, pgTr("launcher.processing.traceLogging.label"));
    m_processingEnableTraceLoggingCheckbox->SetToolTip(pgTr("launcher.processing.traceLogging.tooltip"));
    m_processingEnableTraceLoggingCheckbox->Bind(
        wxEVT_CHECKBOX, &LauncherWindow::onProcessingEnableTraceLoggingChange, this);
    processingCheckboxSizer->Add(m_processingEnableTraceLoggingCheckbox, 0, wxALL, borderSize);

    processingOptionsHorizontalSizer->Add(processingCheckboxSizer, 0, wxALL, borderSize);

    m_processingOptionsSizer->Add(processingOptionsHorizontalSizer, 0, wxALL, 0);

    leftSizer->Add(m_processingOptionsSizer, 1, wxEXPAND | wxALL, borderSize);

    // Add help ? button to the bottom right of the whole window that opens the wiki URL on click.
    auto* helpButton = new wxButton(this, wxID_ANY, "?");
    wxFont helpButtonFont = helpButton->GetFont();
    helpButtonFont.SetPointSize(buttonFontSize); // Set font size to 12
    helpButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
    helpButton->SetFont(helpButtonFont);

    helpButton->SetToolTip(pgTr("launcher.helpButton.tooltip"));

    const wxSize helpBtnSize = FromDIP(wxSize(helpButtonSize, helpButtonSize));
    helpButton->SetMinSize(helpBtnSize);
    helpButton->SetMaxSize(helpBtnSize);

    helpButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) -> void {
        wxLaunchDefaultBrowser("https://github.com/hakasapl/PGPatcher/wiki");
    });

    // Settings (gear) button next to the help button.
    auto* settingsButton = new wxButton(this, wxID_ANY, wxEmptyString);

    wxBitmapBundle settingsIconBundle;
    const std::filesystem::path settingsSVGPath = PGPatcherGlobals::getEXEPath() / "resources" / "settings.svg";
    if (std::filesystem::exists(settingsSVGPath)) {
        std::ifstream settingsSVGStream(settingsSVGPath);
        std::string settingsSVGData(std::istreambuf_iterator<char> { settingsSVGStream },
                                    std::istreambuf_iterator<char> { });
        // The SVG fill is currentColor, which the wx SVG renderer cannot resolve, so substitute the theme color.
        boost::replace_all(settingsSVGData, "currentColor", PGPatcherGlobals::isDarkMode() ? "#ffffff" : "#000000");
        settingsIconBundle
            = wxBitmapBundle::FromSVG(settingsSVGData.c_str(), wxSize(settingsButtonIconSize, settingsButtonIconSize));
    }
    if (settingsIconBundle.IsOk()) {
        settingsButton->SetBitmap(settingsIconBundle);
    } else {
        // Fall back to the gear glyph if the SVG resource is unavailable.
        settingsButton->SetLabel(wxString(wxUniChar(0x2699)));
        wxFont settingsButtonFont = settingsButton->GetFont();
        settingsButtonFont.SetPointSize(buttonFontSize);
        settingsButtonFont.SetWeight(wxFONTWEIGHT_BOLD);
        settingsButton->SetFont(settingsButtonFont);
    }

    settingsButton->SetToolTip(pgTr("launcher.settingsButton.tooltip"));
    settingsButton->SetMinSize(helpBtnSize);
    settingsButton->SetMaxSize(helpBtnSize);
    settingsButton->Bind(wxEVT_BUTTON, &LauncherWindow::onSettingsButtonPressed, this);

    auto* bottomButtonSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomButtonSizer->Add(helpButton, 0, wxRIGHT, borderSize);
    bottomButtonSizer->Add(settingsButton, 0, 0, 0);

    rightSizer->AddStretchSpacer(1);
    rightSizer->Add(bottomButtonSizer, 0, wxALL | wxALIGN_LEFT, borderSize);

    //
    // Finalize.
    //

    columnsSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 0);
    columnsSizer->Add(rightSizer, 0, wxEXPAND | wxALL, 0);

    mainSizer->Add(columnsSizer, 1, wxEXPAND | wxALL, borderSize);

    SetSizerAndFit(mainSizer);
    const auto curSize = GetSize();
    const int minWidth = FromDIP(minWidthDIP);
    SetSize(minWidth, curSize.GetY());
    SetSizeHints(wxSize(minWidth, curSize.GetY()), wxSize(-1, curSize.GetY()));

    Bind(wxEVT_INIT_DIALOG, &LauncherWindow::onInitDialog, this);
}

void LauncherWindow::onInitDialog(wxInitDialogEvent& event)
{
    if (m_initialParams.has_value()) {
        // Launcher rebuilt after a language or theme change: show the unsaved UI state of the previous launcher.
        setUIParams(*m_initialParams);
    } else {
        loadConfig();
    }

    // Trigger the updateDeps event to update the dependencies.
    updateDisabledElements();
    setGamePathBasedOnExe();

    // Call the base class's event handler if needed.
    event.Skip();
}

void LauncherWindow::loadConfig() { setUIParams(m_pgc.getParams()); }

void LauncherWindow::setUIParams(const PGConfig::PGParams& initParams)
{
    // Game.
    if (!m_isGameLocationLocked)
        m_gameLocationTextbox->SetValue(initParams.game.dir.wstring());
    for (const auto& gameType : BethesdaGame::getGameTypes())
        if (gameType == initParams.game.type)
            m_gameTypeRadios[gameType]->SetValue(true);

    // Mod Manager.
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        if (mmType == initParams.modManager.type) {
            m_modManagerRadios[mmType]->SetValue(true);

            // Show MO2 options only if MO2 is selected.
            if (mmType == PGModManager::ModManagerType::MODORGANIZER2) {
                m_mo2InstanceLocationTextbox->Enable(true);
                m_mo2InstanceBrowseButton->Enable(true);
            } else {
                m_mo2InstanceLocationTextbox->Enable(false);
                m_mo2InstanceBrowseButton->Enable(false);
            }
        }
    }

    // MO2-specific options.
    m_mo2InstanceLocationTextbox->SetValue(initParams.modManager.mo2InstanceDir.wstring());

    // Manually trigger the onMO2InstanceLocationChange to populate the listbox.
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly

    // Output.
    m_outputLocationTextbox->SetValue(initParams.output.dir.wstring());
    m_outputZipCheckbox->SetValue(initParams.output.zip);
    m_outputPluginLangCombo->SetStringSelection(PGPlugin::getStringFromPluginLang(initParams.output.pluginLang));

    // Processing.
    m_processingMultithreadingCheckbox->SetValue(initParams.processing.multithread);
    m_processingEnableDevModeCheckbox->SetValue(initParams.processing.enableModDevMode);
    m_processingEnableDebugLoggingCheckbox->SetValue(initParams.processing.enableDebugLogging);
    m_processingEnableTraceLoggingCheckbox->SetValue(initParams.processing.enableTraceLogging);
    m_meshRulesAllowListState = initParams.processing.allowList;
    m_meshRulesBlockListState = initParams.processing.blockList;
    m_textureRulesTextureMapsState = initParams.processing.textureMaps;
    m_dialogRecTypeSelectorState = initParams.processing.allowedModelRecordTypes;

    // Pre-Patchers.
    m_prePatcherFixMeshLightingCheckbox->SetValue(initParams.prePatcher.fixMeshLighting);

    // Shader Patchers.
    m_shaderPatcherParallaxCheckbox->SetValue(initParams.shaderPatcher.parallax);
    m_shaderPatcherComplexMaterialCheckbox->SetValue(initParams.shaderPatcher.complexMaterial);
    m_shaderPatcherTruePBRCheckbox->SetValue(initParams.shaderPatcher.truePBR);

    // Shader Transforms.
    m_shaderTransformParallaxToCMCheckbox->SetValue(initParams.shaderTransforms.parallaxToCM);

    // Post-Patchers.
    m_postPatcherRestoreDefaultShadersCheckbox->SetValue(initParams.postPatcher.disablePrePatchedMaterials);
    m_postPatcherFixSSSCheckbox->SetValue(initParams.postPatcher.fixSSS);
    m_postPatcherHairFlowMapCheckbox->SetValue(initParams.postPatcher.hairFlowMap);

    // Global Patchers.
}

// Component event handlers.

void LauncherWindow::onGameLocationChange([[maybe_unused]] wxCommandEvent& event) { updateDisabledElements(); }

void LauncherWindow::onGameTypeChange([[maybe_unused]] wxCommandEvent& event)
{
    if (m_isGameLocationLocked) {
        updateDisabledElements();
        return;
    }

    const auto initParams = m_pgc.getParams();

    // Update the game location textbox from bethesdagame.
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios[gameType]->GetValue()) {
            if (initParams.game.type == gameType)
                m_gameLocationTextbox->SetValue(initParams.game.dir.wstring());
            else
                m_gameLocationTextbox->SetValue(BethesdaGame::findGamePathFromSteam(gameType).wstring());

            setGamePathBasedOnExe();
            return;
        }
    }

    updateDisabledElements();
}

void LauncherWindow::onModManagerChange([[maybe_unused]] wxCommandEvent& event)
{
    // Show MO2 options only if the MO2 radio button is selected.
    const bool isMO2Selected
        = (event.GetEventObject() == m_modManagerRadios[PGModManager::ModManagerType::MODORGANIZER2]);
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
    DialogModifiableListCtrl dialog(
        this, pgTr("dialogs.meshAllowlist.title"), pgTr("dialogs.meshAllowlist.description"));
    dialog.populateList(m_meshRulesAllowListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesAllowListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onMeshRulesBlockBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogModifiableListCtrl dialog(
        this, pgTr("dialogs.meshBlocklist.title"), pgTr("dialogs.meshBlocklist.description"));
    dialog.populateList(m_meshRulesBlockListState);
    if (dialog.ShowModal() == wxID_OK) {
        m_meshRulesBlockListState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onTextureRulesTextureMapsBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogTextureMapListCtrl dialog(this, pgTr("dialogs.textureRules.title"), pgTr("dialogs.textureRules.description"));
    dialog.populateList(m_textureRulesTextureMapsState);
    if (dialog.ShowModal() == wxID_OK) {
        m_textureRulesTextureMapsState = dialog.getList();
        updateDisabledElements();
    }
}

void LauncherWindow::onSelectPluginTypesBtn([[maybe_unused]] wxCommandEvent& event)
{
    DialogRecTypeSelector selectorDialog(this, pgTr("dialogs.recTypeSelector.title"));
    selectorDialog.populateList(m_dialogRecTypeSelectorState);
    if (selectorDialog.ShowModal() == wxID_OK) {
        m_dialogRecTypeSelectorState = selectorDialog.getSelectedRecordTypes();
        updateDisabledElements();
    }
}

void LauncherWindow::getParams(PGConfig::PGParams& params) const
{
    // Game.
    for (const auto& gameType : BethesdaGame::getGameTypes()) {
        if (m_gameTypeRadios.at(gameType)->GetValue()) {
            params.game.type = gameType;
            break;
        }
    }
    params.game.dir = m_gameLocationTextbox->GetValue().ToStdWstring();

    // Mod Manager.
    for (const auto& mmType : PGModManager::getModManagerTypes()) {
        if (m_modManagerRadios.at(mmType)->GetValue()) {
            params.modManager.type = mmType;
            break;
        }
    }
    params.modManager.mo2InstanceDir = m_mo2InstanceLocationTextbox->GetValue().ToStdWstring();

    // Output.
    params.output.dir = m_outputLocationTextbox->GetValue().ToStdWstring();
    params.output.zip = m_outputZipCheckbox->GetValue();
    params.output.pluginLang
        = PGPlugin::getPluginLangFromString(m_outputPluginLangCombo->GetStringSelection().ToStdString());

    // Processing.
    params.processing.multithread = m_processingMultithreadingCheckbox->GetValue();
    params.processing.enableModDevMode = m_processingEnableDevModeCheckbox->GetValue();
    params.processing.enableDebugLogging = m_processingEnableDebugLoggingCheckbox->GetValue();
    params.processing.enableTraceLogging = m_processingEnableTraceLoggingCheckbox->GetValue();
    params.processing.allowList = m_meshRulesAllowListState;
    params.processing.blockList = m_meshRulesBlockListState;
    params.processing.textureMaps = m_textureRulesTextureMapsState;
    params.processing.allowedModelRecordTypes = m_dialogRecTypeSelectorState;

    // Pre-Patchers.
    params.prePatcher.fixMeshLighting = m_prePatcherFixMeshLightingCheckbox->GetValue();

    // Shader Patchers.
    params.shaderPatcher.parallax = m_shaderPatcherParallaxCheckbox->GetValue();
    params.shaderPatcher.complexMaterial = m_shaderPatcherComplexMaterialCheckbox->GetValue();
    params.shaderPatcher.truePBR = m_shaderPatcherTruePBRCheckbox->GetValue();

    // Shader Transforms.
    params.shaderTransforms.parallaxToCM = m_shaderTransformParallaxToCMCheckbox->GetValue();

    // Post-Patchers.
    params.postPatcher.disablePrePatchedMaterials = m_postPatcherRestoreDefaultShadersCheckbox->GetValue();
    params.postPatcher.fixSSS = m_postPatcherFixSSSCheckbox->GetValue();
    params.postPatcher.hairFlowMap = m_postPatcherHairFlowMapCheckbox->GetValue();

    // Global Patchers.
}

void LauncherWindow::onBrowseGameLocation([[maybe_unused]] wxCommandEvent& event)
{
    if (m_isGameLocationLocked)
        return;

    wxDirDialog dialog(this,
                       pgTr("launcher.browse.gameLocation"),
                       PGConfig::resolveExeRelativePath(m_gameLocationTextbox->GetValue().ToStdWstring()).wstring());
    if (dialog.ShowModal() == wxID_OK)
        m_gameLocationTextbox->SetValue(dialog.GetPath());
}

void LauncherWindow::onBrowseMO2InstanceLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(
        this,
        pgTr("launcher.browse.mo2InstanceLocation"),
        PGConfig::resolveExeRelativePath(m_mo2InstanceLocationTextbox->GetValue().ToStdWstring()).wstring());
    if (dialog.ShowModal() == wxID_OK)
        m_mo2InstanceLocationTextbox->SetValue(dialog.GetPath());

    // Trigger the change event to update the profiles.
    wxCommandEvent changeEvent(wxEVT_TEXT, m_mo2InstanceLocationTextbox->GetId());
    onMO2InstanceLocationChange(changeEvent); // Call the handler directly
}

void LauncherWindow::updateMO2Items()
{
    // Check if MO2 is selected.
    if (!m_modManagerRadios[PGModManager::ModManagerType::MODORGANIZER2]->GetValue()) {
        const bool shouldLock = m_isGameLocationLockedByInstallLocation;
        m_gameLocationTextbox->Enable(!shouldLock);
        m_gameLocationBrowseButton->Enable(!shouldLock);
        m_isGameLocationLocked = shouldLock;
        for (const auto& gameType : BethesdaGame::getGameTypes())
            m_gameTypeRadios[gameType]->Enable(true);
        return;
    }

    // May be relative to the PGPatcher.exe folder (kept as typed in the textbox and the config).
    const auto instanceDir = PGConfig::resolveExeRelativePath(m_mo2InstanceLocationTextbox->GetValue().ToStdWstring());

    // Get game path.
    const auto gamePathMO2 = PGModManager::getGamePathFromInstanceDir(instanceDir);
    const bool lockByMO2Path = !gamePathMO2.empty();
    if (lockByMO2Path) {
        // Found the game path, set it to the game location textbox.
        m_gameLocationTextbox->SetValue(gamePathMO2.wstring());
    }

    const bool shouldLock = m_isGameLocationLockedByInstallLocation || lockByMO2Path;
    m_gameLocationTextbox->Enable(!shouldLock);
    m_gameLocationBrowseButton->Enable(!shouldLock);
    m_isGameLocationLocked = shouldLock;

    // Get game type.
    const auto gameTypeMO2 = PGModManager::getGameTypeFromInstanceDir(instanceDir);
    if (gameTypeMO2 != BethesdaGame::GameType::Unknown) {
        m_gameTypeRadios[gameTypeMO2]->SetValue(true);
        // Disable all radio buttons.
        for (const auto& gameType : BethesdaGame::getGameTypes())
            m_gameTypeRadios[gameType]->Enable(false);
    } else {
        // Enable all radio buttons.
        for (const auto& gameType : BethesdaGame::getGameTypes())
            m_gameTypeRadios[gameType]->Enable(true);
    }
}

void LauncherWindow::onMO2InstanceLocationChange([[maybe_unused]] wxCommandEvent& event) { updateMO2Items(); }

void LauncherWindow::onBrowseOutputLocation([[maybe_unused]] wxCommandEvent& event)
{
    wxDirDialog dialog(this,
                       pgTr("launcher.browse.outputLocation"),
                       PGConfig::resolveExeRelativePath(m_outputLocationTextbox->GetValue().ToStdWstring()).wstring());
    if (dialog.ShowModal() == wxID_OK)
        m_outputLocationTextbox->SetValue(dialog.GetPath());
}

void LauncherWindow::updateDisabledElements()
{
    PGConfig::PGParams curParams = m_pgc.getParams();
    getParams(curParams);

    // Upgrade parallax to CM rules.
    if (curParams.shaderTransforms.parallaxToCM) {
        // Disable and check vanilla parallax patcher.
        m_shaderPatcherParallaxCheckbox->SetValue(true);
        m_shaderPatcherParallaxCheckbox->Enable(false);

        // Disable and check CM patcher.
        m_shaderPatcherComplexMaterialCheckbox->SetValue(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(false);
    } else {
        m_shaderPatcherParallaxCheckbox->Enable(true);
        m_shaderPatcherComplexMaterialCheckbox->Enable(true);
    }

    // Save button.
    m_saveConfigButton->Enable(curParams != m_pgc.getParams());

    // Update output button: only when the current output location holds a previous output that can be updated.
    m_updateOutputButton->Enable(
        !curParams.output.zip && PGRunCache::isUpdateAvailable(PGConfig::resolveExeRelativePath(curParams.output.dir)));

    // Logging checkboxes.
    if (curParams.processing.enableDebugLogging) {
        m_processingEnableTraceLoggingCheckbox->Enable(true);
    } else {
        m_processingEnableTraceLoggingCheckbox->SetValue(false);
        m_processingEnableTraceLoggingCheckbox->Enable(false);
    }
}

void LauncherWindow::onOkButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // All validation passed, proceed with OK actions.
        m_isUpdateRequested = false;
        EndModal(wxID_OK);
    }
}

void LauncherWindow::onUpdateOutputButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        m_isUpdateRequested = true;
        EndModal(wxID_OK);
    }
}

auto LauncherWindow::isUpdateRequested() const -> bool { return m_isUpdateRequested; }

void LauncherWindow::onCancelButtonPressed([[maybe_unused]] wxCommandEvent& event) { wxTheApp->Exit(); }

void LauncherWindow::onSaveConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    if (saveConfig()) {
        // Disable button.
        updateDisabledElements();
    }
}

void LauncherWindow::onLoadConfigButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    const int response = pgMessageBox(pgTr("launcher.confirmLoadConfig.message"),
                                      pgTr("launcher.confirmLoadConfig.title"),
                                      wxYES_NO | wxICON_WARNING,
                                      this);

    if (response != wxYES)
        return;

    // Load the config from the file.
    loadConfig();

    updateDisabledElements();
}

void LauncherWindow::onRestoreDefaultsButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    // Show a confirmation dialog.
    const int response = pgMessageBox(pgTr("launcher.confirmRestoreDefaults.message"),
                                      pgTr("launcher.confirmRestoreDefaults.title"),
                                      wxYES_NO | wxICON_WARNING,
                                      this);

    if (response != wxYES)
        return;

    // Show the defaults in the UI only: the saved config is untouched, so "Save Config" is offered to persist them.
    // And "Load Config" still goes back to the saved config.
    setUIParams(PGConfig::getDefaultParams());

    updateDisabledElements();
}

void LauncherWindow::onSettingsButtonPressed([[maybe_unused]] wxCommandEvent& event)
{
    DialogSettings dialog(this, m_pgc);
    dialog.ShowModal();

    if (dialog.languageChanged() || dialog.themeChanged()) {
        // PGUI::showLauncher reads the current (possibly unsaved) UI state with getParams and passes it to the rebuilt
        // launcher, so the saved config in PGC stays untouched.
        EndModal(resultRelaunch);
    }
}

auto LauncherWindow::saveConfig() -> bool
{
    std::vector<std::string> errors;
    PGConfig::PGParams params = m_pgc.getParams();
    getParams(params);

    // Validate the parameters.
    if (!PGConfig::validateParams(params, errors)) {
        // Validation errors are UTF-8 (translated strings).
        pgMessageBox(
            wxString::FromUTF8(boost::algorithm::join(errors, "\n")), pgTr("common.errors"), wxOK | wxICON_ERROR, this);
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
        m_isGameLocationLockedByInstallLocation = false;
        return;
    }

    auto curParams = m_pgc.getParams();
    getParams(curParams);
    const auto curGameType = curParams.game.type;

    const auto gamePath = exePath.parent_path().parent_path();
    m_isGameLocationLockedByInstallLocation = BethesdaGame::isGamePathValid(gamePath, curGameType);

    const auto curModManagerType = curParams.modManager.type;
    if (curModManagerType == PGModManager::ModManagerType::MODORGANIZER2) {
        // Keep MO2 path selection behavior, but preserve install-location lock precedence.
        updateMO2Items();
        return;
    }

    if (m_isGameLocationLockedByInstallLocation) {
        m_gameLocationTextbox->SetValue(gamePath.wstring());

        // Disable textbox and browse button.
        m_gameLocationTextbox->Enable(false);
        m_gameLocationBrowseButton->Enable(false);
        m_isGameLocationLocked = true;
    } else {
        // Enable textbox and browse button.
        m_gameLocationTextbox->Enable(true);
        m_gameLocationBrowseButton->Enable(true);
        m_isGameLocationLocked = false;
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory,readability-convert-member-functions-to-static)
