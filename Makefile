all: forge_ds_store forge_icon_resource

forge_ds_store: forge_ds_store.cpp
	g++ -o $@ $^
forge_icon_resource: forge_icon_resource.cpp
	g++ -o $@ $^
