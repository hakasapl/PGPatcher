#pragma once

#include "BethesdaGame.hpp"
#include "GUI/components/PGCustomListctrlChangedEvent.hpp"
#include "GUI/components/PGModifiableListCtrl.hpp"
#include "GUI/components/PGTextureMapListCtrl.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenConfig.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <unordered_map>

/**
 * @brief wxDialog that allows the user to configure the ParallaxGen parameters.
 */
class LauncherWindow : public wxDialog {
public:
    /**
     * @brief Construct a new Launcher Window object
     *
     * @param pgc PGC object for UI to use
     */
    LauncherWindow(ParallaxGenConfig& pgc);

    /**
     * @brief Get the Params object (meant to be called after the user presses okay)
     *
     * @param params Reference to PGParams object to fill
     */
    void getParams(ParallaxGenConfig::PGParams& params) const;

private:
    constexpr static int DEFAULT_WIDTH = 600;
    constexpr static int DEFAULT_HEIGHT = 800;
    constexpr static int BORDER_SIZE = 5;
    constexpr static int BUTTON_FONT_SIZE = 12;

    ParallaxGenConfig& m_pgc; /** Reference to the ParallaxGenConfig object */

    /**
     * @brief Runs immediately after the wxDialog gets constructed, intended to set the UI elements to the initial
     * parameters
     *
     * @param event wxWidgets event object
     */
    void onInitDialog(wxInitDialogEvent& event);

    /**
     * @brief Loads config from PGC
     */
    void loadConfig();

    //
    // UI Param Elements
    //

    // Game
    wxTextCtrl* m_gameLocationTextbox;
    void onGameLocationChange(wxCommandEvent& event);
    wxButton* m_gameLocationBrowseButton;

    std::unordered_map<BethesdaGame::GameType, wxRadioButton*> m_gameTypeRadios;
    void onGameTypeChange(wxCommandEvent& event);

    // Mod Manager
    std::unordered_map<ModManagerDirectory::ModManagerType, wxRadioButton*> m_modManagerRadios;
    void onModManagerChange(wxCommandEvent& event);

    wxTextCtrl* m_mo2InstanceLocationTextbox;
    void onMO2InstanceLocationChange(wxCommandEvent& event);

    // Output
    wxTextCtrl* m_outputLocationTextbox;
    void onOutputLocationChange(wxCommandEvent& event);

    wxCheckBox* m_outputZipCheckbox;
    void onOutputZipChange(wxCommandEvent& event);

    // Advanced
    wxCheckBox* m_advancedOptionsCheckbox;
    void onAdvancedOptionsChange(wxCommandEvent& event);

    // Processing
    wxCheckBox* m_processingPluginPatchingCheckbox;
    void onProcessingPluginPatchingChange(wxCommandEvent& event);
    wxStaticBoxSizer* m_processingPluginPatchingOptions;
    wxCheckBox* m_processingPluginPatchingOptionsESMifyCheckbox;
    void onProcessingPluginPatchingOptionsESMifyChange(wxCommandEvent& event);
    wxComboBox* m_processingPluginPatchingOptionsLangCombo;
    void onProcessingPluginPatchingOptionsLangChange(wxCommandEvent& event);

    wxCheckBox* m_processingMultithreadingCheckbox;
    void onProcessingMultithreadingChange(wxCommandEvent& event);

    wxCheckBox* m_processingBSACheckbox;
    void onProcessingBSAChange(wxCommandEvent& event);

    wxCheckBox* m_processingEnableDebugLoggingCheckbox;
    void onProcessingEnableDebugLoggingChange(wxCommandEvent& event);

    wxCheckBox* m_processingEnableTraceLoggingCheckbox;
    void onProcessingEnableTraceLoggingChange(wxCommandEvent& event);

    // Pre-Patchers
    wxCheckBox* m_prePatcherDisableMLPCheckbox;
    void onPrePatcherDisableMLPChange(wxCommandEvent& event);

    wxCheckBox* m_prePatcherFixMeshLightingCheckbox;
    void onPrePatcherFixMeshLightingChange(wxCommandEvent& event);

    // Shader Patchers
    wxCheckBox* m_shaderPatcherParallaxCheckbox;
    void onShaderPatcherParallaxChange(wxCommandEvent& event);

    wxCheckBox* m_shaderPatcherComplexMaterialCheckbox;
    void onShaderPatcherComplexMaterialChange(wxCommandEvent& event);

    wxStaticBoxSizer* m_shaderPatcherComplexMaterialOptionsSizer; /** Stores the complex material options */
    PGModifiableListCtrl* m_shaderPatcherComplexMaterialDynCubemapBlocklist;
    void onShaderPatcherComplexMaterialDynCubemapBlocklistChange(PGCustomListctrlChangedEvent& event);

    wxCheckBox* m_shaderPatcherTruePBRCheckbox;
    void onShaderPatcherTruePBRChange(wxCommandEvent& event);

    wxStaticBoxSizer* m_shaderPatcherTruePBROptionsSizer;
    wxCheckBox* m_shaderPatcherTruePBRCheckPathsCheckbox;
    void onShaderPatcherTruePBRCheckPathsChange(wxCommandEvent& event);

    wxCheckBox* m_shaderPatcherTruePBRPrintNonExistentPathsCheckbox;
    void onShaderPatcherTruePBRPrintNonExistentPathsChange(wxCommandEvent& event);

    // Shader Transforms
    wxStaticBoxSizer* m_shaderTransformParallaxToCMSizer;
    wxCheckBox* m_shaderTransformParallaxToCMCheckbox;
    void onShaderTransformParallaxToCMChange(wxCommandEvent& event);

    wxCheckBox* m_shaderTransformParallaxToCMOnlyWhenRequiredCheckbox;
    void onShaderTransformParallaxToCMOnlyWhenRequiredChange(wxCommandEvent& event);

    // Post-Patchers
    wxCheckBox* m_postPatcherRestoreDefaultShadersCheckbox;
    void onPostPatcherRestoreDefaultShadersChange(wxCommandEvent& event);

    wxCheckBox* m_postPatcherFixSSSCheckbox;
    void onPostPatcherFixSSSChange(wxCommandEvent& event);

    wxCheckBox* m_postPatcherHairFlowMapCheckbox;
    void onPostPatcherHairFlowMapChange(wxCommandEvent& event);

    // Global Patchers
    wxCheckBox* m_globalPatcherFixEffectLightingCSCheckbox;
    void onGlobalPatcherFixEffectLightingCSChange(wxCommandEvent& event);

    // Mesh Rules
    PGModifiableListCtrl* m_meshRulesAllowList;
    void onMeshRulesAllowListChange(PGCustomListctrlChangedEvent& event);

    PGModifiableListCtrl* m_meshRulesBlockList;
    void onMeshRulesBlockListChange(PGCustomListctrlChangedEvent& event);

    // Texture Rules
    PGTextureMapListCtrl* m_textureRulesMaps;

    PGModifiableListCtrl* m_textureRulesVanillaBSAList;
    void onTextureRulesVanillaBSAListChange(PGCustomListctrlChangedEvent& event);

    //
    // UI Controls
    //
    wxStaticBoxSizer* m_mo2OptionsSizer; /** Stores the MO2-specific options since these are only sometimes shown */
    wxStaticBoxSizer* m_processingOptionsSizer; /** Stores the processing options */

    void onTextureRulesMapsChange(PGCustomListctrlChangedEvent& event);

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for
     * game location
     *
     * @param event wxWidgets event object
     */
    void onBrowseGameLocation(wxCommandEvent& event);

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for MO2
     * instance location
     *
     * @param event wxWidgets event object
     */
    void onBrowseMO2InstanceLocation(wxCommandEvent& event);

    /**
     * @brief Event handler responsible for showing the brose dialog when the user clicks on the browse button - for
     * output location
     *
     * @param event wxWidgets event object
     */
    void onBrowseOutputLocation(wxCommandEvent& event);

    /**
     * @brief Updates disabled elements based on the current state of the UI
     */
    void updateDisabledElements();

    void updateMO2Items();

    wxBoxSizer* m_advancedOptionsSizer; /** Container that stores advanced options */
    void updateAdvanced();

    //
    // Validation
    //
    wxButton* m_okButton; /** Stores the OKButton as a member var in case it needs to be disabled/enabled */
    wxButton*
        m_saveConfigButton; /** Stores the SaveConfigButton as a member var in case it needs to be disabled/enabled */
    wxButton* m_loadConfigButton;

    /**
     * @brief Event handler that triggers when the user presses "Start Patching" - performs validation
     *
     * @param event wxWidgets event object
     */
    void onOkButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Save Config" button
     *
     * @param event wxWidgets event object
     */
    void onSaveConfigButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Load Config" button
     *
     * @param event wxWidgets event object
     */
    void onLoadConfigButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the "Restore Defaults" button
     *
     * @param event wxWidgets event object
     */
    void onRestoreDefaultsButtonPressed(wxCommandEvent& event);

    /**
     * @brief Event handler that triggers when the user presses the X on the dialog window, which closes the application
     *
     * @param event wxWidgets event object
     */
    void onClose(wxCloseEvent& event);

    /**
     * @brief Saves current values to the config
     */
    auto saveConfig() -> bool;

    /**
     * @brief Set the Game Path Based On Exe location
     */
    void setGamePathBasedOnExe();
};
