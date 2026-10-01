// Retail script packages must stay unchanged for online play and servers only accept the retail GUIController,
// so this autoloaded page points the retail main menu options at the fixed pages.
class FixMenuSetup extends GUIPage;

function InitComponent( GUIController MyController, GUIComponent MyOwner )
{
	class'UT2K4GUIController'.default.MainMenuOptions[1] = "GUI2K4Fix.FixServerBrowser";
	class'UT2K4GUIController'.default.MainMenuOptions[2] = "GUI2K4Fix.FixGamePageMP";
	class'UT2K4GUIController'.default.MainMenuOptions[3] = "GUI2K4Fix.FixGamePageSP";
	class'UT2K4GUIController'.default.MainMenuOptions[5] = "GUI2K4Fix.FixSettingsPage";
}
