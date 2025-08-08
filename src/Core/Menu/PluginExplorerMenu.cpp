#include "Core/Menu/PluginExplorerMenu.hpp"
#include "Core/Menu/PluginExplorer.hpp"
#include "Core/Menu/Items/ItemForm.hpp"
#include "Core/Menu/Items/ItemPlugin.hpp"

#include "Core/Config.hpp"
#include "Core/Locale/LocaleManager.hpp"

#include "Scaleform/System/Logger.hpp"

namespace Core::Menu
{
	PluginExplorerMenu::PluginExplorerMenu() :
		Super(),
		RE::MenuEventHandler()
	{
		auto menu = static_cast<Super*>(this);
		menu->inputContext = Context::kItemMenu;
		menu->depthPriority = SORT_PRIORITY;
		menu->menuFlags.set(
			Flag::kUsesMenuContext,
			Flag::kDisablePauseMenu,
			Flag::kAllowSaving,
			Flag::kHasButtonBar,
			Flag::kUsesMovementToDirection);

		auto& config = Config::Get();
		if (config.PluginExplorer.Pause)
			menu->menuFlags.set(Flag::kPausesGame);

		auto scaleform = RE::BSScaleformManager::GetSingleton();
		bool success = scaleform->LoadMovieEx(menu, FILE_NAME, [](RE::GFxMovieDef* a_def) -> void {
			using StateType = RE::GFxState::StateType;
			a_def->SetState(StateType::kLog, RE::make_gptr<SF::Logger<PluginExplorerMenu>>().get());
		});

		if (!success) {
			auto message = fmt::format("{} did not have a view due to missing dependencies!\nSkyUI SE must be installed.", MENU_NAME);
			stl::report_and_fail(message);
		}

		_view = menu->uiMovie;
		_view->SetMouseCursorCount(0);

		Init();
		InitExtensions();

		auto mc = RE::MenuControls::GetSingleton();
		mc->RegisterHandler(this);
	}

	PluginExplorerMenu::~PluginExplorerMenu()
	{
		auto mc = RE::MenuControls::GetSingleton();
		mc->RemoveHandler(this);
	}

	auto PluginExplorerMenu::ProcessMessage(RE::UIMessage& a_message)
		-> RE::UI_MESSAGE_RESULTS
	{
		using Message = RE::UI_MESSAGE_TYPE;
		using Result = RE::UI_MESSAGE_RESULTS;
		switch (*a_message.type) {
			case Message::kShow:
				OnOpen();
				return Result::kHandled;
			case Message::kHide:
			case Message::kForceHide:
				OnClose();
				return Result::kHandled;
			case Message::kUpdateController:
				RefreshPlatform();
				return Result::kPassOn;
			default:
				return Super::ProcessMessage(a_message);
		}
	}

	void PluginExplorerMenu::AdvanceMovie(float a_interval, uint32_t a_currentTime)
	{
		if ((_upHeld > 0) || (_downHeld > 0)) {
			_heldGuard += 1;
			if (_heldGuard >= 15) {
				_heldCount += 1;
				if (_heldCount >= 5) {
					if (_upHeld)
						ModSelectedIndex(-1);
					else if (_downHeld)
						ModSelectedIndex(1);

					_heldCount = 0;
				}
			}
		} else {
			_heldGuard = 0;
			_heldCount = 0;
		}

		Super::AdvanceMovie(a_interval, a_currentTime);
	}

	bool PluginExplorerMenu::CanProcess(RE::InputEvent* a_event)
	{
		using Type = RE::INPUT_EVENT_TYPE;

		if (!a_event)
			return false;

		if (!a_event->eventType.all(Type::kButton))
			return false;

		return true;
	}

	bool PluginExplorerMenu::ProcessButton(RE::ButtonEvent* event)
	{
		using Device = RE::INPUT_DEVICE;
		auto device = event->GetDevice();
		bool isKeyReleased = event->IsUp();
		bool isKeyPressed = event->IsDown();

		enum class Action
		{
			None,
			MoveUp,
			MoveDown,
			PageUp,
			PageDown,
			Select,
			Back
		};

		Action action = Action::None;

		switch (device) {
			case Device::kKeyboard: {
				using Key = RE::BSWin32KeyboardDevice::Key;
				switch (event->idCode) {
					case Key::kW:
					case Key::kUp: action = Action::MoveUp; break;
					case Key::kS:
					case Key::kDown: action = Action::MoveDown; break;
					case Key::kPageUp: action = Action::PageUp; break;
					case Key::kPageDown: action = Action::PageDown; break;
					case Key::kD:
					case Key::kRight:
					case Key::kEnter: action = Action::Select; break;
					case Key::kA:
					case Key::kLeft:
					case Key::kEscape:
					case Key::kTab: action = Action::Back; break;
				}
				break;
			}
			case Device::kMouse: {
				using Key = RE::BSWin32MouseDevice::Key;
				switch (event->idCode) {
					case Key::kLeftButton: action = Action::Select; break;
					case Key::kRightButton: action = Action::Back; break;
					case Key::kWheelUp: action = Action::MoveUp; break;
					case Key::kWheelDown: action = Action::MoveDown; break;
				}
				break;
			}
			case Device::kGamepad: {
				auto& eventName = event->QUserEvent();
				auto  userEvents = RE::UserEvents::GetSingleton();

				if (eventName == userEvents->accept) {
					action = Action::Select;
				} else if (eventName == userEvents->cancel) {
					action = Action::Back;
				} else if (eventName == userEvents->up) {
					action = Action::MoveUp;
				} else if (eventName == userEvents->down) {
					action = Action::MoveDown;
				} else if (eventName == userEvents->right || eventName == userEvents->pageUp) {
					action = Action::PageUp;
				} else if (eventName == userEvents->left || eventName == userEvents->pageDown) {
					action = Action::PageDown;
				}
				break;
			}
			default: break;
		}

		switch (action) {
			case Action::MoveUp:
				_upHeld = isKeyPressed;
				if (isKeyPressed) {
					_upHeld = true;
					ModSelectedIndex(-1);
				}
				break;
			case Action::MoveDown:
				_downHeld = isKeyPressed;
				if (isKeyPressed) {
					_downHeld = true;
					ModSelectedIndex(1);
				}
				break;
			case Action::PageUp:
				if (isKeyPressed) {
					ModSelectedIndex(-16);
				}
				break;
			case Action::PageDown:
				if (isKeyPressed) {
					ModSelectedIndex(16);
				}
				break;
			case Action::Select:
				if (isKeyPressed) {
					Select();
				}
				break;
			case Action::Back:
				if (isKeyPressed) {
					Back();
				}
				break;
			case Action::None:
				break;
		}

		if (isKeyReleased) {
			_upHeld = false;
			_heldGuard = 0;
			_heldCount = 0;
		}

		return true;
	}

	bool PluginExplorerMenu::IsOpen()
	{
		auto ui = RE::UI::GetSingleton();
		return ui->IsMenuOpen(MENU_NAME);
	}

	void PluginExplorerMenu::Open()
	{
		auto queue = RE::UIMessageQueue::GetSingleton();
		queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
	}

	void PluginExplorerMenu::Close()
	{
		auto queue = RE::UIMessageQueue::GetSingleton();
		queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
	}

	void PluginExplorerMenu::Toggle()
	{
		if (IsOpen())
			Close();
		else
			Open();
	}

	void PluginExplorerMenu::Init()
	{
		using element_t = std::pair<std::reference_wrapper<SF::Object>, std::string_view>;
		std::array objects{
			element_t{ std::ref(_rootObj), "_root.rootObj"sv },
			element_t{ std::ref(_title), "_root.rootObj.title"sv },
			element_t{ std::ref(_pluginList), "_root.rootObj.itemList"sv },
			element_t{ std::ref(_formList), "_root.rootObj.formList"sv },
			element_t{ std::ref(_buttonBar), "_root.rootObj.buttonBar"sv }
		};

		for (const auto& [object, path] : objects) {
			auto&                       instance = object.get().GetInstance();
			[[maybe_unused]] const bool success = _view->GetVariable(std::addressof(instance), path.data());
			SF::Assert(success && instance.IsObject());
		}

		_rootObj.Visible(true);

		_title.AutoSize(SF::Object{ "left" });
		_title.Visible(false);

		_pluginList.Init(_view);
		_formList.Init(_view);

		_view->CreateArray(std::addressof(_buttonBarProvider));
		_buttonBar.DataProvider(SF::Array{ _buttonBarProvider });

		Refresh();

		auto container = PluginExplorer::GetContainer();
		if (container) {
			auto player = RE::PlayerCharacter::GetSingleton();
			container->SetParentCell(player->GetParentCell());
			container->SetPosition({ player->GetPositionX(), player->GetPositionY(), -2000 });
			container->SetCollision(false);
		} else {
			logger::critical("No container");
		}
	}

	void PluginExplorerMenu::InitExtensions()
	{
		const RE::GFxValue boolean{ true };
		SF::Assert(_view->SetVariable("_global.gfxExtensions", boolean));
		SF::Assert(_view->SetVariable("_global.noInvisibleAdvance", boolean));
	}

	void PluginExplorerMenu::OnOpen()
	{
		using UEFlag = RE::ControlMap::UEFlag;
		if (const auto control = RE::ControlMap::GetSingleton()) {
			control->ToggleControls(UEFlag::kPOVSwitch, false);
		}

		if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::HUDMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}

	void PluginExplorerMenu::OnClose()
	{
		using UEFlag = RE::ControlMap::UEFlag;
		if (const auto control = RE::ControlMap::GetSingleton()) {
			control->ToggleControls(UEFlag::kPOVSwitch, true);
		}

		if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::HUDMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
	}

	void PluginExplorerMenu::Refresh()
	{
		UpdatePosition();
		RefreshPlugins();

		if (_focus == Focus::ContainerLoop) {
			_focus = Focus::Form;
			_pluginList.RestoreIndex(_pluginListIndex);
			_pluginList.Visible(false);
			RefreshForms();
			_formList.RestoreIndex(_formListIndex);
		} else {
			_focus = Focus::Plugin;
			_pluginName = "";
			_pluginIndex = 0;
			_pluginListIndex = 0;
			_formName = "";
			_formType = RE::FormType::None;
			_formListIndex = 0;
		}

		RefreshUI();
	}

	void PluginExplorerMenu::RefreshPlugins()
	{
		const auto idx = _pluginList.SelectedIndex();
		_pluginList.clear();

		auto& plugins = PluginExplorer::GetPlugins();
		for (auto& [index, plugin] : plugins) {
			if (plugin.GetCount() == 0)
				continue;

			auto itemPlugin = std::make_shared<Item::ItemPlugin>(index, plugin.GetName(), plugin.GetCount());
			_pluginList.push_back(itemPlugin);
		}

		_pluginList.Refresh();
		_pluginList.RestoreIndex(idx);
	}

	void PluginExplorerMenu::RefreshForms()
	{
		if (_pluginName.empty())
			return;

		const auto idx = _formList.SelectedIndex();
		_formList.clear();

		auto plugin = PluginExplorer::FindPlugin(_pluginIndex);
		if (plugin) {
			auto& types = plugin->GetForms();
			auto  doForms = [&](RE::FormType a_type) {
                if (types.contains(a_type)) {
                    auto itemForm = std::make_shared<Item::ItemForm>(a_type, types[a_type].size());
                    _formList.push_back(itemForm);
                }
			};

			using Type = RE::FormType;
			doForms(Type::AlchemyItem);
			doForms(Type::Ammo);
			doForms(Type::Armor);
			doForms(Type::Book);
			doForms(Type::Ingredient);
			doForms(Type::KeyMaster);
			doForms(Type::Misc);
			doForms(Type::Note);
			doForms(Type::Scroll);
			doForms(Type::SoulGem);
			doForms(Type::Spell);
			doForms(Type::Weapon);
		}

		_formList.Refresh();
		_formList.RestoreIndex(idx);
		_formList.Visible(true);
	}

	void PluginExplorerMenu::RefreshUI()
	{
		UpdateTitle();
		UpdateButtonBar();
	}

	void PluginExplorerMenu::ModSelectedIndex(double a_mod)
	{
		if (_focus == Focus::Plugin) {
			_pluginList.ModSelectedIndex(a_mod);
			_pluginListIndex = _pluginList.SelectedIndex();
		} else if (_focus == Focus::Form) {
			_formList.ModSelectedIndex(a_mod);
			_formListIndex = _formList.SelectedIndex();
		}
	}

	void PluginExplorerMenu::ModSelectedPage(double a_mod)
	{
		if (_focus == Focus::Plugin)
			_pluginList.InvokeA("modSelectedPage", nullptr, a_mod);
		else if (_focus == Focus::Form)
			_formList.InvokeA("modSelectedPage", nullptr, a_mod);
	}

	void PluginExplorerMenu::Select()
	{
		if (_focus == Focus::Plugin) {
			auto plugin = _pluginList.SelectedItem<Item::ItemPlugin>();
			if (plugin) {
				_focus = Focus::Form;
				_pluginName = plugin->GetName();
				_pluginIndex = plugin->GetIndex();
				_pluginList.Visible(false);
				RefreshForms();
			}
		} else if (_focus == Focus::Form) {
			auto form = _formList.SelectedItem<Item::ItemForm>();
			if (form) {
				_focus = Focus::Container;
				_formName = form->GetName();
				_formType = form->GetType();
				Close();
			}
		}

		RefreshUI();
	}

	void PluginExplorerMenu::Back()
	{
		if (_focus == Focus::Plugin) {
			Close();
		} else if (_focus == Focus::Form) {
			_focus = Focus::Plugin;
			_formList.Visible(false);
			_formList.SelectedIndex(0);
			_pluginList.Visible(true);
			_pluginName.clear();
			_pluginIndex = 0;
			_formType = RE::FormType::None;
		}

		RefreshUI();
	}

	void PluginExplorerMenu::UpdatePosition()
	{
		auto def = _view->GetMovieDef();
		if (def) {
			//_rootObj.X(
			//	_rootObj.X() + def->GetWidth() / 5);
		}
	}

	void PluginExplorerMenu::UpdateTitle()
	{
		std::string str = "";
		if (_focus == Focus::Form) {
			str = _pluginName;
		} else {
			auto locale = LocaleManager::GetSingleton();
			auto locStr = LocaleStrings::GetSingleton();
			str = locale->Translate(locStr->plugPlugins);
		}

		_title.HTMLText(str);
		_title.Visible(true);
	}

	void PluginExplorerMenu::UpdateButtonBar()
	{
		if (!_view)
			return;

		auto userEvent = RE::UserEvents::GetSingleton();

		_buttonBarProvider.ClearElements();
		auto gmst = RE::GameSettingCollection::GetSingleton();

		auto makeButton = [&](std::string_view a_index, const char* a_label) {
			RE::GFxValue obj;
			_view->CreateObject(std::addressof(obj));
			auto setting = gmst->GetSetting(a_label);
			obj.SetMember("label", { static_cast<std::string_view>(setting->GetString()) });
			obj.SetMember("index", { a_index });
			_buttonBarProvider.PushBack(obj);
		};

		makeButton(userEvent->accept, "sAccept");
		if (_focus == Focus::Plugin)
			makeButton(userEvent->cancel, "sCancel");
		else
			makeButton(userEvent->cancel, "sBack");

		_buttonBar.InvalidateData();
	}
}
