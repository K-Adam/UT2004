class FixDetailSettings extends UT2K4Tab_DetailSettings;

// The retail lists offer D3D and Pixo as renderers and full shadows only on D3D, so the OpenGL settings match no entry.
// A GUI2K4 compiled from this repo already lists full shadows for OpenGL.
function InitializeCombos()
{
	Super.InitializeCombos();
	co_RenderDevice.ResetComponent();
	co_RenderDevice.AddItem("OpenGL",, "OpenGLDrv.OpenGLRenderDevice");
	if (co_Shadows.ItemCount() < ArrayCount(ShadowOptions))
		co_Shadows.AddItem(ShadowOptions[2]);
}
