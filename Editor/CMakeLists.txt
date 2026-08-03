ut_module(Editor SHARED
  SOURCES Editor.cpp TTFontImport.cpp UAnalyzeBuild.cpp UAnalyzeContentCommandlet.cpp
          UBatchExportCommandlet.cpp UBrushBuilder.cpp UConformCommandlet.cpp
          UContentCommandlets.cpp UConvertMaterialCommandlet.cpp UCutdownContentCommandlet.cpp
          UDumpIntCommandlet.cpp UDXTConvertCommandlet.cpp UGroupRepairCommandlet.cpp
          UMakeCommandlet.cpp UMapConvertCommandlet.cpp UMasterCommandlet.cpp UnBsp.cpp
          UnDumpConfigCommandlet.cpp UnEdCnst.cpp UnEdCsg.cpp UnEdExp.cpp UnEdFact.cpp
          UnEditor.cpp UnEdLevelStat.cpp UnEdSrv.cpp UnEdSrvExecImporters.cpp UnEdTran.cpp
          UnLightwaveImporter.cpp UnMeshEd.cpp UnMeshLP.cpp UnParams.cpp UnPrefab.cpp
          UnScrCom.cpp UnShadow.cpp UnStaticMesh.cpp UnTopics.cpp UnVisi.cpp
          UPkgCommandlet.cpp URebuildCommandlet.cpp
          USetNormalLODCommandlet.cpp USoundLocCommandlet.cpp UStripSourceCommandlet.cpp
          USurfaceTypeCommandlet.cpp UTextureInfoCommandlet.cpp UTextureLODCommandlet.cpp
          UTextureStripCommandlet.cpp xEdInt.cpp xObjExporters.cpp xTextureConversions.cpp
  PCH EditorPrivate.h Editor.cpp
  DEPS Window)
target_compile_definitions(Editor PRIVATE _EDITOR_)
