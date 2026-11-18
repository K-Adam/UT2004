/*=============================================================================
	UnFoliage.h: Replacement plants drawn in place of retail foliage meshes.
=============================================================================*/

// The plants come from plain files in the Foliage folder and never become objects of a package, so
// collision, scripts and the network see the retail meshes alone.
class ENGINE_API FFoliage
{
public:
	// Draws a decoration layer's sector with its replacement. Returns 0 when the retail mesh has to draw.
	static UBOOL RenderDecorations(ATerrainInfo* Terrain,INT LayerIndex,INT SectorIndex,FLevelSceneNode* SceneNode,FRenderInterface* RI);
	// How far the wind carries the top of the plant that replaces a mesh, per unit of the layer's scale.
	static FLOAT WindReach(FLevelSceneNode* SceneNode,UStaticMesh* StaticMesh);
	// Drops what was built from a terrain's decorations, for one sector or all of them.
	static void ForgetTerrain(ATerrainInfo* Terrain,INT SectorIndex = INDEX_NONE);

	static UBOOL ReplacesActor(FLevelSceneNode* SceneNode,AActor* Actor);
	// Draws the replacements of the collected actors and empties the list.
	static void RenderActors(TArray<FDynamicActor*>& Actors,FLevelSceneNode* SceneNode,FRenderInterface* RI);
};
