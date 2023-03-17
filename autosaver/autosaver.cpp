#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iterator>
#include <string_view>

#include <aviutl/filter.hpp>
#include <winwrap.hpp>
#include <json.h>
#include "scope_exit.hpp"

using namespace std::string_view_literals;

auto& get_state() {
	struct State {
		std::filesystem::path setting_path;
		std::filesystem::path unsaved_project_autosave_dir;
		std::chrono::system_clock::time_point last_saved{ std::chrono::system_clock::now() };
		AviUtl::EditHandle** adr_editp{};
		BOOL(__fastcall* save_project)(AviUtl::EditHandle*, LPCSTR) {};
	};
	static State s;

	return s;
}

auto& get_setting() {
	struct Setting {
		std::chrono::seconds duration{ 300 };

		void load(const std::filesystem::path& path) {
			const auto fsize = file_size(path);
			std::string buf(static_cast<size_t>(fsize), '\0');
			std::ifstream ifs{ path }; std::getline(ifs, buf, '\0');

			json_value_s* root{};
			SCOPE_EXIT_AUTO{ [root] { free(root); } };

			root = json_parse(buf.c_str(), buf.size());
			if (root == nullptr) {
				return;
			}

			auto obj = json_value_as_object(root);
			if (obj == nullptr)return;

			for (auto elm = obj->start; elm != nullptr; elm = elm->next) {
				if (elm->name->string == "duration"sv) {
					if (auto n = json_value_as_number(elm->value)) {
						duration = std::chrono::seconds{ [n]{
							long long value;
							std::from_chars(n->number, n->number + n->number_size, value);
							return value;
						}() };
					}
				}
			}
		}

		void store(const std::filesystem::path& path) const {
			std::ofstream ofs{ path };

			ofs << "{\n\t\"duration\" : " << [](long long x) {
				std::string ret(std::numeric_limits<long long>::digits10 + 2, '\0');
				std::to_chars(ret.data(), ret.data() + ret.size(), x);
				ret.resize(ret.find_first_of('\0'));
				return ret;
			}(duration.count()) << "\n}\n";
		}
	};
	static Setting s;

	return s;
}

void save_project(const std::filesystem::path& path) {
	auto& state = get_state();
	state.save_project(*state.adr_editp, path.string().c_str());
}

BOOL __cdecl func_init(AviUtl::FilterPlugin* fp) {
	AviUtl::SysInfo si; fp->exfunc->get_sys_info(nullptr, &si);
	if (si.build != 11003) {
		MessageBoxW(fp->hwnd_parent, L"バージョン1.10のAviUtlが必要です。", L"autosaver", MB_ICONINFORMATION);
		return FALSE;
	}

	auto& state = get_state();
	auto& setting = get_setting();
	 
	auto self_dir = std::filesystem::path{ WinWrap::Module{ fp->dll_hinst }.getFileNameW() }.parent_path();
	state.setting_path = self_dir / L"autosaver.json";
	state.unsaved_project_autosave_dir = self_dir / "autosaver";

	WinWrap::Module aviutl{};
	auto aviutl_base = reinterpret_cast<uintptr_t>(aviutl.getHandle());
	state.adr_editp = reinterpret_cast<decltype(state.adr_editp)>(aviutl_base + 0x08717c);
	state.save_project = reinterpret_cast<decltype(state.save_project)>(aviutl_base + 0x024160);

	if (std::filesystem::exists(state.setting_path)) {
		setting.load(state.setting_path);
	}
	else {
		setting.store(state.setting_path);
	}

	return TRUE;
}

BOOL __cdecl func_proc(AviUtl::FilterPlugin* fp, AviUtl::FilterProcInfo* fpip) {
	auto& state = get_state();
	auto& setting = get_setting();

	const auto now = std::chrono::system_clock::now();
	if (now - state.last_saved <= setting.duration) return TRUE;

	state.last_saved = now;

	AviUtl::SysInfo si; fp->exfunc->get_sys_info(fpip->editp, &si);

	std::filesystem::path autosave_dir = [proj = si.project_name, &state] {
		if (proj && proj[0] != '\0') {
			return std::filesystem::path{ proj }.parent_path();
		}
		else {
			return state.unsaved_project_autosave_dir;
		}
	}();

	if (!std::filesystem::exists(autosave_dir)) {
		std::filesystem::create_directory(autosave_dir);
	}

	save_project(autosave_dir / std::format(L"{:%Y-%m-%d-%H-%M-%S}.aup", std::chrono::floor<std::chrono::seconds>(std::chrono::local_time{ now.time_since_epoch() })));

	return TRUE;
}

using Flag = AviUtl::FilterPluginDLL::Flag;
AviUtl::FilterPluginDLL filter{
	.flag = Flag::AlwaysActive | Flag::DispFilter | Flag::ExInformation,
	.name = "autosaver",
	.func_proc = func_proc,
	.func_init = func_init,
	.information = "autosaver r2 by ePi",
};

auto __stdcall GetFilterTable() {
	return &filter;
}
