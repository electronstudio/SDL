md dist
copy DebugDLL\Win32\SDL2.dll dist\SDL2Win32Debug.dll
copy DebugDLL\Win32\SDL2.pdb dist\SDL2Win32Debug.dll.pdb
copy ReleaseDLL\Win32\SDL2.dll dist\SDL2Win32Release.dll
copy ReleaseDLL\Win32\SDL2.dll dist\SDL2.dll
copy ReleaseDLL\Win32\SDL2.pdb dist\SDL2Win32Release.dll.pdb
copy DebugDLL\x64\SDL2.dll dist\SDL2Win64Debug.dll
copy DebugDLL\x64\SDL2.pdb dist\SDL2Win64Debug.dll.pdb
copy ReleaseDLL\x64\SDL2.dll dist\SDL2Win64Release.dll
copy ReleaseDLL\x64\SDL2.pdb dist\SDL2Win64Release.dll.pdb
copy ..\..\libs\SDL2Win* dist\
pause