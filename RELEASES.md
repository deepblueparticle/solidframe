# SolidFrame Releases

## Version 2.0
### TODO
* (DONE) Fix utility memoryfile examples
* (DONE) pushCall instead of pushReinit
* (PENDING) Finalizing the IPC library - what is planned for SFv2.0.
* Cross-platform support:
	* (DONE) Linux
	* (DONE) FreeBSD
	* (WAIT) Darwin/OSX (currently no support thread_local, will be supported by XCode 8)
* (DONE) Add "make install" support.
* Documentation.
* Preparing for SFv2.0.
* SolidFrame version 2.0.

## Version 2.x
### TODO
* solid::any<>: use shared_ptr for objects that do not fit the emplace buffer
* Test serialization for all supported std containers
* Test serialization engine agaist ProtoBuf and FlatBuffers
* frame/ipc with SOCKS5
* Improved OpenSSL support in frame/aio
* frame/ipc with OpenSSL
