#include "librecomp/config.hpp"
#include "librecomp/mods.hpp"
#include "librecomp/files.hpp"
#include <iostream>
#include <stdexcept>

recomp::mods::ModOpenError parse_config_option_dependencies(const nlohmann::json&, recomp::config::Config&);
recomp::mods::ModOpenError parse_manifest_config_schema_option(const nlohmann::json&, recomp::config::Config*, std::string&);
namespace recomp {
bool Version::from_string(const std::string&, Version&) { throw std::logic_error("unexpected version parsing"); }
const Version& get_project_version() { throw std::logic_error("unexpected project version access"); }
std::filesystem::path get_config_path() { throw std::logic_error("unexpected config filesystem access"); }
std::ifstream open_input_backup_file(const std::filesystem::path&, std::ios_base::openmode) { throw std::logic_error("unexpected backup read"); }
std::ofstream open_output_file_with_backup(const std::filesystem::path&, std::ios_base::openmode) { throw std::logic_error("unexpected backup write"); }
bool finalize_output_file_with_backup(const std::filesystem::path&) { throw std::logic_error("unexpected backup finalization"); }
}
namespace recomp::mods {
void ModContext::add_opened_mod(ModManifest&&, recomp::config::Config&&, std::vector<size_t>&&,
                               std::vector<ModContentTypeId>&&, std::vector<char>&&) {
    throw std::logic_error("unexpected mod loading");
}
}
// Archive and filesystem paths are excluded from this focused parser test.
extern "C" {
mz_bool mz_zip_reader_end(mz_zip_archive*) { throw std::logic_error("unexpected archive operation"); }
mz_bool mz_zip_reader_init_cfile(mz_zip_archive*, MZ_FILE*, mz_uint64, mz_uint) { throw std::logic_error("unexpected archive operation"); }
mz_bool mz_zip_reader_init_mem(mz_zip_archive*, const void*, size_t, mz_uint) { throw std::logic_error("unexpected archive operation"); }
mz_bool mz_zip_reader_locate_file_v2(mz_zip_archive*, const char*, const char*, mz_uint, mz_uint32*) { throw std::logic_error("unexpected archive operation"); }
mz_bool mz_zip_reader_file_stat(mz_zip_archive*, mz_uint, mz_zip_archive_file_stat*) { throw std::logic_error("unexpected archive operation"); }
mz_bool mz_zip_reader_extract_to_mem(mz_zip_archive*, mz_uint, void*, size_t, mz_uint) { throw std::logic_error("unexpected archive operation"); }
}
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main() {
    using namespace recomp::config;
    using recomp::mods::ModOpenError;
    try {
        Config config("Dependency test", "test");
        config.add_bool_option("toggle", "Toggle", "", false);
        config.add_bool_option("dependent", "Dependent", "", false);
        config.add_enum_option("mode", "Mode", "", std::vector<ConfigOptionEnumOption>{{0, "Normal"}, {1, "Custom"}}, 0u);
        nlohmann::json option = {
            {"id", "dependent"},
            {"disabled_from", {{"id", "toggle"}, {"matches", true}}},
            {"hidden_from", {{"id", "mode"}, {"matches", {"Custom"}}}},
        };
        require(parse_config_option_dependencies(option, config) == ModOpenError::Good, "valid dependency rejected");
        config.set_option_value("toggle", true);
        require(config.is_config_option_disabled(1), "parsed disabled dependency was discarded");
        config.set_option_value("toggle", false);
        require(!config.is_config_option_disabled(1), "disabled dependency did not clear");
        config.set_option_value("mode", uint32_t{1});
        require(config.is_config_option_hidden(1), "parsed hidden dependency was discarded");
        config.set_option_value("mode", uint32_t{0});
        require(!config.is_config_option_hidden(1), "hidden dependency did not clear");
        option.erase("disabled_from");
        option["hidden_from"]["matches"] = {"Custom", "Missing"};
        require(parse_config_option_dependencies(option, config) == ModOpenError::InvalidHiddenOptionDependency,
                "invalid enum dependency was partially accepted");
        option["hidden_from"]["id"] = "unknown_option";
        require(parse_config_option_dependencies(option, config) == ModOpenError::InvalidHiddenOptionDependency,
                "unknown dependency source was accepted");
        require(config.get_config_schema().options_by_id.size() == 3, "failed dependency added a phantom option");
        nlohmann::json numeric = {{"id", "amount"}, {"name", "Amount"}, {"type", "Number"},
                                  {"min", 5}, {"max", 2}, {"step", 0}};
        std::string error;
        require(parse_manifest_config_schema_option(numeric, &config, error) == ModOpenError::IncorrectConfigSchemaType,
                "reversed numeric bounds were accepted");
        numeric["max"] = 10;
        numeric["step"] = -1;
        require(parse_manifest_config_schema_option(numeric, &config, error) == ModOpenError::IncorrectConfigSchemaType,
                "negative numeric step was accepted");
        numeric["step"] = 0; // Zero retains the continuous-slider convention.
        require(parse_manifest_config_schema_option(numeric, &config, error) == ModOpenError::Good,
                "valid continuous numeric option was rejected");
        require(std::get<double>(config.get_option_value("amount")) == 5.0,
                "omitted numeric default escaped declared bounds");
        std::cout << "PASS: parsed mod dependencies persist, update controls, and reject partial invalid enum lists\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
