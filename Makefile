# compile with HYPRLAND_HEADERS=<path_to_hl> make all
# make sure that the path above is to the root hl repo directory, NOT src/
# and that you have ran `make protocols` in the hl dir.

# This Makefile is not intended to be used directly, but rather as a template for your own plugin
# Change the PLUGIN_NAME variable to the name of your plugin
PLUGIN_NAME=hyprlens

# Enable parallel builds
MAKEFLAGS := --jobs=$(shell nproc)
MAKEFLAGS += --output-sync=target

# Source files
SOURCE_FILES=$(wildcard src/*.cpp)
# Add any new source file directories to the SOURCE_FILES variable
# SOURCE_FILES+=<new_source_dir>/*.cpp

# Header files, used to check if they have changed
INCLUDE_FILES=$(wildcard include/*.hpp)
INCLUDE_FILES+=$(wildcard include/*.h)
# Add any new header file directories to the INCLUDE_FILES variable
# INCLUDE_FILES+=<new_header_dir>/*.hpp

# Intermediate object files
OBJECT_DIR=obj

# Compiler flags
COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=-fdiagnostics-color=always
COMPILE_FLAGS+=`pkg-config --cflags hyprland pixman-1 libdrm`
COMPILE_FLAGS+=-Iinclude

# Linker flags, set to shared library (plugin)
LINK_FLAGS=-shared

COMPILE_DEFINES=-DWLR_USE_UNSTABLE

ifeq ($(shell whereis -b jq), "jq:")
$(error "jq not found. Please install jq.")
else
BUILT_WITH_NOXWAYLAND=$(shell hyprctl version -j | jq -r '.flags | .[]' | grep 'no xwayland')
ifneq ($(BUILT_WITH_NOXWAYLAND),)
COMPILE_DEFINES+=-DNO_XWAYLAND
endif
endif

# Phony targets (i.e. targets that don't actually build anything, and don't track dependencies)
# These will always be run when called
.PHONY: clean clangd

# build
all: check_env $(PLUGIN_NAME).so

# install to ~/.local/share/hyprload/plugins/bin, assuming that hyprload is installed
install: all
	cp $(PLUGIN_NAME).so ${HOME}/.local/share/hyprload/plugins/bin

# ensure that HYPRLAND_HEADERS is set, otherwise error. Gives a more helpful error message than just include errors
check_env:
	mkdir -p $(OBJECT_DIR)
	@if pkg-config --exists hyprland; then \
		echo 'Hyprland headers found.'; \
	else \
		echo 'Hyprland headers not available. Run `make pluginenv` in the root Hyprland directory.'; \
		exit 1; \
	fi
	@if [ -z $(BUILT_WITH_NOXWAYLAND) ]; then \
		echo 'Building with XWayland support.'; \
	else \
		echo 'Building without XWayland support.'; \
	fi

# build the plugin, using the headers from the configured Hyprland repo
$(OBJECT_DIR)/%.o: src/%.cpp $(INCLUDE_FILES)
	g++ -c -o $@ $< $(COMPILE_FLAGS)

$(PLUGIN_NAME).so: $(addprefix $(OBJECT_DIR)/, $(notdir $(SOURCE_FILES:.cpp=.o)))
	g++ $(LINK_FLAGS) -o $@ $^ $(COMPILE_FLAGS)

# clean up
clean:
	rm -f $(OBJECT_DIR)/*.o
	rm -f ./$(PLUGIN_NAME).so

# generate compile_flags.txt for clangd, if you use it. This is not necessary for building the plugin
# the alternative is to use the compile_commands.json file generated by eg. bear
clangd:
	echo "$(COMPILE_FLAGS) $(COMPILE_DEFINES)" | \
	sed 's/--no-gnu-unique//g' | \
	sed 's/ -/\n-/g' | \
	sed 's/std=c++23/std=c++2b/g' \
	> compile_flags.txt
