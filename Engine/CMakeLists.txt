set(ENGINE_SRC
  AFileLog.cpp AGameStats.cpp ASceneSubtitles.cpp AxEmitter.cpp AxPickupBase.cpp AxProcMesh.cpp
  AxWeatherEffect.cpp Engine.cpp KarmaSupport.cpp KConstraint.cpp KDebugDraw.cpp KDynStep.cpp
  KFarfield.cpp KIntersectFunctions.cpp KPhysic.cpp KScript.cpp KSkeletal.cpp KTriListGen.cpp
  KUtils.cpp LIPSincSupport.cpp NullDrv.cpp UExportCacheCommandlet.cpp ULodMesh.cpp UnActCol.cpp
  UnActor.cpp UnAudio.cpp UnBeamEmitter.cpp UnBunch.cpp UnCacheManager.cpp UnCamera.cpp
  UnCameraEffects.cpp UnCamMgr.cpp UnCanvas.cpp UnCDKey.cpp UnChan.cpp UnConn.cpp UnController.cpp
  UnConvexVolume.cpp UnDemoPenLev.cpp UnDemoRec.cpp UnDownload.cpp UnEngine.cpp UnErrorChecking.cpp
  UnFluidSurface.cpp UnFoliage.cpp UnFont.cpp UnFPoly.cpp UnGame.cpp UnGameUtilities.cpp UnIn.cpp
  UnInteraction.cpp UnInteractionMaster.cpp UnkDOP.cpp UnLevAct.cpp UnLevel.cpp UnLevTic.cpp
  UnLodMesh.cpp UnManifest.cpp UnMaterial.cpp UnMatineeTools.cpp UnMesh.cpp UnMeshEmitter.cpp
  UnModel.cpp UnModelLight.cpp UnMover.cpp UnNavigationPoint.cpp UnNetDrv.cpp UnOctree.cpp
  UnPackageCheckInfo.cpp UnParams.cpp UnParticleEmitter.cpp UnParticleSystem.cpp UnPath.cpp
  UnPawn.cpp UnPenLev.cpp UnPhysic.cpp UnPlayer.cpp UnPlayInfo.cpp UnPrim.cpp UnProjector.cpp
  UnReach.cpp UnRebuildTools.cpp UnRemoteCallGuard.cpp UnRender.cpp UnRenderBatch.cpp UnRenderBSP.cpp
  UnRenderEditorActor.cpp UnRenderLight.cpp UnRenderPool.cpp UnRenderResource.cpp
  UnRenderStaticMesh.cpp UnRenderUtil.cpp UnRenderVisibility.cpp UnRoute.cpp UnSceneManager.cpp
  UnScript.cpp UnScriptedTexture.cpp UnSecurity.cpp UnShadowProjector.cpp UnSkeletalMesh.cpp
  UnSkeletalMeshCollision.cpp UnSkeletalTools.cpp UnSparkEmitter.cpp UnSpriteEmitter.cpp
  UnStatGraph.cpp UnStaticMesh.cpp UnStaticMeshBuild.cpp UnStaticMeshCollision.cpp UnStats.cpp
  UnSVehicle.cpp UnTerrain.cpp UnTerrainTools.cpp UnTex.cpp UnTexReplace.cpp UnTrace.cpp UnTrailEmitter.cpp UnTrunk.cpp UnURL.cpp
  UnVehicle.cpp UnVertMesh.cpp UnVoiceChat.cpp UnVolume.cpp USpline.cpp UStreamInterface.cpp
  xDataObject.cpp xForceFeedback.cpp xParticleMgr.cpp)

ut_module(Engine SHARED
  SOURCES ${ENGINE_SRC}
  PCH EnginePrivate.h AFileLog.cpp
  NOPCH UnCDKey.cpp UStreamInterface.cpp UExportCacheCommandlet.cpp)
target_link_libraries(Engine PRIVATE nvtristrip karma::karma ${SQUISH_TARGET} PUBLIC Core)
