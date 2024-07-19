#include "ParallaxGen/ParallaxGen.hpp"

#include <spdlog/spdlog.h>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <fstream>
#include <DirectXTex.h>

#include "ParallaxGenUtil/ParallaxGenUtil.hpp"

using namespace std;
namespace fs = filesystem;
using namespace nifly;

ParallaxGen::ParallaxGen(const fs::path output_dir, ParallaxGenDirectory* pgd, ParallaxGenD3D* pgd3d, bool optimize_meshes)
{
    // constructor
    this->output_dir = output_dir;
    this->pgd = pgd;
	this->pgd3d = pgd3d;

	// ! TODO normalize these paths before string comparing
	if (boost::iequals(this->output_dir.wstring(), this->pgd->getDataPath().wstring())) {
		spdlog::critical("Output directory cannot be your data folder, as meshes can be overwritten this way. Exiting.");
		ParallaxGenUtil::exitWithUserInput(1);
	}

	// set optimize meshes flag
	nif_save_options.optimize = optimize_meshes;
}

void ParallaxGen::upgradeShaders()
{
	spdlog::info("Attempting to upgrade shaders where possible...");

	//loop through height maps
	size_t finished_task = 0;

	auto heightMaps = pgd->getHeightMaps();
	size_t num_upgrades = heightMaps.size();
	for (fs::path height_map : heightMaps) {
		if (finished_task % 10 == 0) {
			double progress = (double)finished_task / num_upgrades * 100.0;
			spdlog::info("Shader Upgrades Processed: {}/{} ({:.1f}%)", finished_task, num_upgrades, progress);
		}

		// Replace "_p" with "_m" in the stem
		fs::path env_map_path = pgd->changeDDSSuffix(height_map, L"_m");
		fs::path complex_map_path = env_map_path;

		if (!pgd->isFile(env_map_path))
		{
			// no env map
			env_map_path = fs::path();
		}

		// upgrade to complex material
		DirectX::ScratchImage new_ComplexMap = pgd3d->upgradeToComplexMaterial(height_map, env_map_path);

		// save to file
		if (new_ComplexMap.GetImageCount() > 0) {
			fs::path output_path = output_dir / complex_map_path;
			fs::create_directories(output_path.parent_path());

			HRESULT hr = DirectX::SaveToDDSFile(new_ComplexMap.GetImages(), new_ComplexMap.GetImageCount(), new_ComplexMap.GetMetadata(), DirectX::DDS_FLAGS_NONE, output_path.c_str());
			if (FAILED(hr)) {
				spdlog::error(L"Unable to save complex material {}: {}", env_map_path.wstring(), hr);
			}

			// add newly created file to complexMaterialMaps for later processing
			pgd->addComplexMaterialMap(complex_map_path);
		}

		finished_task++;
	}
}

void ParallaxGen::patchMeshes()
{
	// patch meshes
	// loop through each mesh nif file
	size_t finished_task = 0;

	auto meshes = pgd->getMeshes();
	auto heightMaps = pgd->getHeightMaps();
	auto complexMaterialMaps = pgd->getComplexMaterialMaps();
	size_t num_meshes = meshes.size();
	for (fs::path mesh : meshes) {
		if (finished_task % 100 == 0) {
			double progress = (double)finished_task / num_meshes * 100.0;
			spdlog::info("NIFs Processed: {}/{} ({:.1f}%)", finished_task, num_meshes, progress);
		}

		processNIF(mesh, heightMaps, complexMaterialMaps);
		finished_task++;
	}

	// create state file
	ofstream state_file(output_dir / parallax_state_file);
	state_file.close();
}

void ParallaxGen::zipMeshes() {
	// zip meshes
	spdlog::info("Zipping meshes...");
	zipDirectory(output_dir, output_dir / "ParallaxGen_Output.zip");
}

void ParallaxGen::deleteMeshes() {
	// delete meshes
	spdlog::info("Cleaning up meshes generated by ParallaxGen...");
	// Iterate through the folder
	for (const auto& entry : fs::directory_iterator(output_dir)) {
		if (fs::is_directory(entry.path())) {
			// Remove the directory and all its contents
			try {
				fs::remove_all(entry.path());
				spdlog::trace(L"Deleted directory {}", entry.path().wstring());
			}
			catch (const exception& e) {
				spdlog::error(L"Error deleting directory {}: {}", entry.path().wstring(), ParallaxGenUtil::convertToWstring(e.what()));
			}
		}

		// remove state file
		if (entry.path().filename().wstring() == L"PARALLAXGEN_DONTDELETE") {
			try {
				fs::remove(entry.path());
			}
			catch (const exception& e) {
				spdlog::error(L"Error deleting state file {}: {}", entry.path().wstring(), ParallaxGenUtil::convertToWstring(e.what()));
			}
		}
	}
}

void ParallaxGen::deleteOutputDir() {
	// delete output directory
	if (fs::exists(output_dir) && fs::is_directory(output_dir)) {
		spdlog::info("Deleting existing ParallaxGen output...");

		try {
			for (const auto& entry : fs::directory_iterator(output_dir)) {
                fs::remove_all(entry.path());
            }
		}
		catch (const exception& e) {
			spdlog::critical(L"Error deleting output directory {}: {}", output_dir.wstring(), ParallaxGenUtil::convertToWstring(e.what()));
			ParallaxGenUtil::exitWithUserInput(1);
		}
	}
}

// shorten some enum names
typedef BSLightingShaderPropertyShaderType BSLSP;
typedef SkyrimShaderPropertyFlags1 SSPF1;
typedef SkyrimShaderPropertyFlags2 SSPF2;
void ParallaxGen::processNIF(const fs::path& nif_file, vector<fs::path>& heightMaps, vector<fs::path>& complexMaterialMaps)
{
	const fs::path output_file = output_dir / nif_file;
	if (fs::exists(output_file)) {
		spdlog::error(L"Unable to process NIF file, file already exists: {}", nif_file.wstring());
		return;
	}

	// process nif file
	vector<std::byte> nif_file_data = pgd->getFile(nif_file);

	if (nif_file_data.empty()) {
		spdlog::warn(L"Unable to read NIF file (ignoring): {}", nif_file.wstring());
		return;
	}

	boost::iostreams::array_source nif_array_source(reinterpret_cast<const char*>(nif_file_data.data()), nif_file_data.size());
	boost::iostreams::stream<boost::iostreams::array_source> nif_stream(nif_array_source);

	// load nif file
	NifFile nif;

	try {
		// try block for loading nif
		//!TODO if NIF is a loose file nifly should load it directly
		nif.Load(nif_stream);
	}
	catch (const exception& e) {
		spdlog::warn(L"Error reading NIF file (ignoring): {}, {}", nif_file.wstring(), ParallaxGenUtil::convertToWstring(e.what()));
		return;
	}

	if (!nif.IsValid()) {
		spdlog::warn(L"Invalid NIF file (ignoring): {}", nif_file.wstring());
		return;
	}

	bool nif_modified = false;

	// ignore nif if has attached havok animations
	vector<NiObject*> block_tree;
	nif.GetTree(block_tree);

	// loop through blocks
	for (NiObject* block : block_tree) {
		if (block->GetBlockName() == "BSBehaviorGraphExtraData") {
			spdlog::trace(L"Rejecting NIF file {} due to attached havok animations", nif_file.wstring());
			return;
		}
	}

	// loop through each node in nif
	for (NiShape* shape : nif.GetShapes()) {
		const auto block_id = nif.GetBlockID(shape);

		// exclusions
		// get shader type
		if (!shape->HasShaderProperty()) {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: No shader property", block_id, nif_file.wstring());
			continue;
		}

		// only allow BSLightingShaderProperty blocks
		string shape_block_name = shape->GetBlockName();
		if (shape_block_name != "NiTriShape" && shape_block_name != "BSTriShape") {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: Incorrect shape block type", block_id, nif_file.wstring());
			continue;
		}

		// ignore skinned meshes, these don't support parallax
		if (shape->HasSkinInstance() || shape->IsSkinned()) {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: Skinned mesh", block_id, nif_file.wstring());
			continue;
		}

		// get shader from shape
		NiShader* shader = nif.GetShader(shape);
		if (shader == nullptr) {
			// skip if no shader
			spdlog::trace(L"Rejecting shape {} in NIF file {}: No shader", block_id, nif_file.wstring());
			continue;
		}

		// check that shader has a texture set
		if (!shader->HasTextureSet()) {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: No texture set", block_id, nif_file.wstring());
			continue;
		}

		string shader_block_name = shader->GetBlockName();
		if (shader_block_name != "BSLightingShaderProperty") {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: Incorrect shader block type", block_id, nif_file.wstring());
			continue;
		}

		// don't enable parallax on decals because that gets rid of blending
		BSLightingShaderProperty* cur_bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);

		if (cur_bslsp->shaderFlags2 & SSPF2::SLSF2_BACK_LIGHTING) {
			spdlog::trace(L"Rejecting shape {} in NIF file {}: Back lighting shape", block_id, nif_file.wstring());
			continue;
		}

		// Get shader type for later use
		BSLSP shader_type = static_cast<BSLSP>(shader->GetShaderType());

		// build search vector
		vector<string> search_prefixes;
		// diffuse map lookup first
		string diffuse_map;
		uint32_t diffuse_result = nif.GetTextureSlot(shape, diffuse_map, 0);
		if (diffuse_result == 0) {
			continue;
		}
		ParallaxGenUtil::addUniqueElement(search_prefixes, diffuse_map.substr(0, diffuse_map.find_last_of('.')));
		// normal map lookup
		string normal_map;
		uint32_t normal_result = nif.GetTextureSlot(shape, normal_map, 1);
		if (diffuse_result > 0) {
			ParallaxGenUtil::addUniqueElement(search_prefixes, normal_map.substr(0, normal_map.find_last_of('_')));
		}

		// check if meshes should be changed
		for (string& search_prefix : search_prefixes) {
			// check if complex material file exists
			fs::path search_path;
			string search_prefix_lower = boost::algorithm::to_lower_copy(search_prefix);

			// processing for complex material
            search_path = search_prefix_lower + "_m.dds";
            if (find(complexMaterialMaps.begin(), complexMaterialMaps.end(), search_path) != complexMaterialMaps.end()) {
				if (shader_type != BSLSP::BSLSP_DEFAULT && shader_type != BSLSP::BSLSP_ENVMAP && shader_type != BSLSP::BSLSP_PARALLAX) {
					spdlog::trace(L"Rejecting shape {} in NIF file {}: Incorrect shader type", block_id, nif_file.wstring());
					continue;
				}

				// verify that maps match each other
				if (!hasSameAspectRatio(diffuse_map, search_path)) {
					spdlog::trace(L"Rejecting shape {} in NIF file {}: Height map does not match diffuse map", block_id, nif_file.wstring());
					continue;
				}

                // Enable complex parallax for this shape!
                nif_modified |= enableComplexMaterialOnShape(nif, shape, shader, search_prefix);
                break;  // don't check anything else
            }

			// processing for parallax
            search_path = search_prefix_lower + "_p.dds";
            if (find(heightMaps.begin(), heightMaps.end(), search_path) != heightMaps.end()) {
				// decals don't work with regular parallax
				if (cur_bslsp->shaderFlags1 & SSPF1::SLSF1_DECAL || cur_bslsp->shaderFlags1 & SSPF1::SLSF1_DYNAMIC_DECAL) {
					spdlog::trace(L"Rejecting shape {} in NIF file {}: Decal shape", block_id, nif_file.wstring());
					continue;
				}

                // Enable regular parallax for this shape!
				if (shader_type != BSLSP::BSLSP_DEFAULT && shader_type != BSLSP::BSLSP_PARALLAX) {
					// don't overwrite existing shaders
					spdlog::trace(L"Rejecting shape {} in NIF file {}: Incorrect shader type", block_id, nif_file.wstring());
					continue;
				}

				// verify that maps match each other
				if (!hasSameAspectRatio(diffuse_map, search_path)) {
					spdlog::trace(L"Rejecting shape {} in NIF file {}: Height map does not match diffuse map", block_id, nif_file.wstring());
					continue;
				}

				// enable parallax on mesh!
                nif_modified |= enableParallaxOnShape(nif, shape, shader, search_prefix);
                break;
            }
		}
	}

	// save NIF if it was modified
	if (nif_modified) {
		spdlog::debug(L"NIF Patched: {}", nif_file.wstring());

		// create directories if required
		fs::create_directories(output_file.parent_path());

		if (nif.Save(output_file, nif_save_options)) {
			spdlog::error(L"Unable to save NIF file: {}", nif_file.wstring());
		}
	}
}

bool ParallaxGen::enableComplexMaterialOnShape(NifFile& nif, NiShape* shape, NiShader* shader, const string& search_prefix)
{
	// enable complex material on shape
	bool changed = false;
	// 1. set shader type to env map
	if (shader->GetShaderType() != BSLSP::BSLSP_ENVMAP) {
		shader->SetShaderType(BSLSP::BSLSP_ENVMAP);
		changed = true;
	}
	// 2. set shader flags
	BSLightingShaderProperty* cur_bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);
	if(cur_bslsp->shaderFlags1 & SSPF1::SLSF1_PARALLAX) {
		// Complex material cannot have parallax shader flag
		cur_bslsp->shaderFlags1 &= ~SSPF1::SLSF1_PARALLAX;
	}

	if (!(cur_bslsp->shaderFlags1 & SSPF1::SLSF1_ENVIRONMENT_MAPPING)) {
		cur_bslsp->shaderFlags1 |= SSPF1::SLSF1_ENVIRONMENT_MAPPING;
		changed = true;
	}
	// 3. set vertex colors for shape
	if (!shape->HasVertexColors()) {
		shape->SetVertexColors(true);
		changed = true;
	}
	// 4. set vertex colors for shader
	if (!shader->HasVertexColors()) {
		shader->SetVertexColors(true);
		changed = true;
	}
	// 5. set complex material texture
	string height_map;
	uint32_t height_result = nif.GetTextureSlot(shape, height_map, 3);
	if (height_result != 0 || !height_map.empty()) {
		// remove height map
		string new_height_map = "";
		nif.SetTextureSlot(shape, new_height_map, 3);
		changed = true;
	}

	string env_map;
	uint32_t env_result = nif.GetTextureSlot(shape, env_map, 5);
	if (!boost::iends_with(env_map, ".dds")) {
		// add height map
		string new_env_map = search_prefix + "_m.dds";
		nif.SetTextureSlot(shape, new_env_map, 5);
		changed = true;
	}
	return changed;
}

bool ParallaxGen::enableParallaxOnShape(NifFile& nif, NiShape* shape, NiShader* shader, const string& search_prefix)
{
	// enable parallax on shape
	bool changed = false;
	// 1. set shader type to parallax
	if (shader->GetShaderType() != BSLSP::BSLSP_PARALLAX) {
		shader->SetShaderType(BSLSP::BSLSP_PARALLAX);
		changed = true;
	}
	// 2. Set shader flags
	BSLightingShaderProperty* cur_bslsp = dynamic_cast<BSLightingShaderProperty*>(shader);
	if(cur_bslsp->shaderFlags1 & SSPF1::SLSF1_ENVIRONMENT_MAPPING) {
		// Vanilla parallax cannot have environment mapping flag
		cur_bslsp->shaderFlags1 &= ~SSPF1::SLSF1_ENVIRONMENT_MAPPING;
	}

	if (!(cur_bslsp->shaderFlags1 & SSPF1::SLSF1_PARALLAX)) {
		cur_bslsp->shaderFlags1 |= SSPF1::SLSF1_PARALLAX;
		changed = true;
	}
	// 3. set vertex colors for shape
	if (!shape->HasVertexColors()) {
		shape->SetVertexColors(true);
		changed = true;
	}
	// 4. set vertex colors for shader
	if (!shader->HasVertexColors()) {
		shader->SetVertexColors(true);
		changed = true;
	}
	// 5. set parallax heightmap texture
	string height_map;
	uint32_t height_result = nif.GetTextureSlot(shape, height_map, 3);
	
	// Check if existing heightmap ends with .dds
	// Sometimes the height_map is textures\\ and nothing else so we need to check for that
	// There may be a more ideal solution for this (ie. why is nifly reporting textures this way?)
	if (!boost::iends_with(height_map, ".dds")) {
		// add height map
		string new_height_map = search_prefix + "_p.dds";
		nif.SetTextureSlot(shape, new_height_map, 3);
		changed = true;
	}

	return changed;
}

bool ParallaxGen::hasSameAspectRatio(const fs::path& dds_path_1, const fs::path& dds_path_2)
{
	// verify that maps match each other
	auto check_tuple = make_tuple(fs::path(boost::algorithm::to_lower_copy(dds_path_1.wstring())), fs::path(boost::algorithm::to_lower_copy(dds_path_2.wstring())));
	if (height_map_checks.find(check_tuple) != height_map_checks.end()) {
		// key already exists
		return height_map_checks[check_tuple];
	} else {
		// Need to perform computation
		return pgd3d->checkIfAspectRatioMatches(dds_path_1, dds_path_2);
	}
}

void ParallaxGen::addFileToZip(mz_zip_archive& zip, const fs::path& filePath, const fs::path& zipPath)
{
	// ignore zip file itself
	if (filePath == zipPath) {
		return;
	}

	// open file stream
	vector<std::byte> buffer = ParallaxGenUtil::getFileBytes(filePath);

	// get relative path
	fs::path zip_relative_path = filePath.lexically_relative(output_dir);

	string zip_file_path = ParallaxGenUtil::wstring_to_utf8(zip_relative_path.wstring());

	// add file to zip
    if (!mz_zip_writer_add_mem(&zip, zip_file_path.c_str(), buffer.data(), buffer.size(), MZ_NO_COMPRESSION)) {
		spdlog::error(L"Error adding file to zip: {}", filePath.wstring());
		ParallaxGenUtil::exitWithUserInput(1);
    }
}

void ParallaxGen::zipDirectory(const fs::path& dirPath, const fs::path& zipPath)
{
	mz_zip_archive zip;

	// init to 0
    memset(&zip, 0, sizeof(zip));

	// check if directory exists
	if (!fs::exists(dirPath)) {
		spdlog::info("No outputs were created");
		ParallaxGenUtil::exitWithUserInput(0);
	}

	// Check if file already exists and delete
	if (fs::exists(zipPath)) {
		spdlog::info(L"Deleting existing output zip file: {}", zipPath.wstring());
		fs::remove(zipPath);
	}

	// initialize file
	string zip_path_string = ParallaxGenUtil::wstring_to_utf8(zipPath);
    if (!mz_zip_writer_init_file(&zip, zip_path_string.c_str(), 0)) {
		spdlog::critical(L"Error creating zip file: {}", zipPath.wstring());
		ParallaxGenUtil::exitWithUserInput(1);
    }

	// add each file in directory to zip
    for (const auto &entry : fs::recursive_directory_iterator(dirPath)) {
        if (fs::is_regular_file(entry.path())) {
            addFileToZip(zip, entry.path(), zipPath);
        }
    }

	// finalize zip
    if (!mz_zip_writer_finalize_archive(&zip)) {
		spdlog::critical(L"Error finalizing zip archive: {}", zipPath.wstring());
		ParallaxGenUtil::exitWithUserInput(1);
    }

    mz_zip_writer_end(&zip);

	spdlog::info(L"Please import this file into your mod manager: {}", zipPath.wstring());
}
