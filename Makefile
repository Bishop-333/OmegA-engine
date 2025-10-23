
# Quake3 Unix Makefile
#
# Nov '98 by Zoid <zoid@idsoftware.com>
#
# Loki Hacking by Bernd Kreimeier
#  and a little more by Ryan C. Gordon.
#  and a little more by Rafael Barrero
#  and a little more by the ioq3 cr3w
#
# GNU Make required
#
COMPILE_PLATFORM=$(shell uname | sed -e 's/_.*//' | tr '[:upper:]' '[:lower:]' | sed -e 's/\//_/g')
COMPILE_ARCH=$(shell uname -m | sed -e 's/i.86/x86/' | sed -e 's/^arm.*/arm/')

ifeq ($(shell uname -m),arm64)
  COMPILE_ARCH=aarch64
endif

ifeq ($(COMPILE_PLATFORM),mingw32)
  ifeq ($(COMPILE_ARCH),i386)
    COMPILE_ARCH=x86
  endif
endif

BUILD_CLIENT     = 1
BUILD_SERVER     = 1

USE_SDL          = 1
USE_CURL         = 1
USE_LOCAL_HEADERS= 0
USE_SYSTEM_JPEG  = 0

USE_OGG_VORBIS    = 1
USE_SYSTEM_OGG    = 0
USE_SYSTEM_VORBIS = 0

USE_VULKAN       = 1
USE_OPENGL       = 0
USE_OPENGL2      = 0
USE_OPENGL_API   = 0
USE_VULKAN_API   = 1
USE_RENDERER_DLOPEN = 0

# valid options: vulkan
RENDERER_DEFAULT = vulkan

CNAME            = quake3e
DNAME            = quake3e.ded

RENDERER_PREFIX  = $(CNAME)


ifeq ($(V),1)
echo_cmd=@:
Q=
else
echo_cmd=@echo
Q=@
endif

#############################################################################
#
# If you require a different configuration from the defaults below, create a
# new file named "Makefile.local" in the same directory as this file and define
# your parameters there. This allows you to change configuration without
# causing problems with keeping up to date with the repository.
#
#############################################################################
-include Makefile.local

ifeq ($(COMPILE_PLATFORM),darwin)
  USE_SDL=1
  USE_LOCAL_HEADERS=1
  USE_RENDERER_DLOPEN = 0
endif

ifeq ($(COMPILE_PLATFORM),cygwin)
  PLATFORM=mingw32
endif

ifndef PLATFORM
PLATFORM=$(COMPILE_PLATFORM)
endif
export PLATFORM

ifeq ($(PLATFORM),mingw32)
  MINGW=1
endif
ifeq ($(PLATFORM),mingw64)
  MINGW=1
endif

ifeq ($(COMPILE_ARCH),i86pc)
  COMPILE_ARCH=x86
endif

ifeq ($(COMPILE_ARCH),amd64)
  COMPILE_ARCH=x86_64
endif
ifeq ($(COMPILE_ARCH),x64)
  COMPILE_ARCH=x86_64
endif

ifndef ARCH
ARCH=$(COMPILE_ARCH)
endif
export ARCH

ifneq ($(PLATFORM),$(COMPILE_PLATFORM))
  CROSS_COMPILING=1
else
  CROSS_COMPILING=0

  ifneq ($(ARCH),$(COMPILE_ARCH))
    CROSS_COMPILING=1
  endif
endif
export CROSS_COMPILING

ifndef DESTDIR
DESTDIR=/usr/local/games/quake3
endif

ifndef MOUNT_DIR
MOUNT_DIR=src
endif

ifndef BUILD_DIR
BUILD_DIR=build
endif

ifndef GENERATE_DEPENDENCIES
GENERATE_DEPENDENCIES=1
endif

ifndef USE_CCACHE
USE_CCACHE=0
endif
export USE_CCACHE

ifndef USE_LOCAL_HEADERS
USE_LOCAL_HEADERS=1
endif

ifndef USE_CURL
USE_CURL=1
endif

ifndef USE_CURL_DLOPEN
  ifdef MINGW
    USE_CURL_DLOPEN=0
  else
    USE_CURL_DLOPEN=1
  endif
endif

ifndef USE_OGG_VORBIS
  USE_OGG_VORBIS=1
endif

ifndef USE_SYSTEM_OGG
  USE_SYSTEM_OGG=1
endif

ifndef USE_SYSTEM_VORBIS
  USE_SYSTEM_VORBIS=1
endif

ifeq ($(USE_RENDERER_DLOPEN),0)
  ifeq ($(RENDERER_DEFAULT),opengl)
    USE_OPENGL=1
    USE_OPENGL2=0
    USE_VULKAN=0
    USE_OPENGL_API=1
    USE_VULKAN_API=0
  endif
  ifeq ($(RENDERER_DEFAULT),opengl2)
    USE_OPENGL=0
    USE_OPENGL2=1
    USE_VULKAN=0
    USE_OPENGL_API=1
    USE_VULKAN_API=0
  endif
  ifeq ($(RENDERER_DEFAULT),vulkan)
    USE_OPENGL=0
    USE_OPENGL2=0
    USE_VULKAN=1
    USE_OPENGL_API=0
  endif
endif

ifneq ($(USE_VULKAN),0)
  USE_VULKAN_API=1
endif


#############################################################################

BD=$(BUILD_DIR)/debug-$(PLATFORM)-$(ARCH)
BR=$(BUILD_DIR)/release-$(PLATFORM)-$(ARCH)
ADIR=$(MOUNT_DIR)/platform/asm
CDIR=$(MOUNT_DIR)/game/client
SDIR=$(MOUNT_DIR)/game/server
RDIR=$(MOUNT_DIR)/engine/renderer
SDLDIR=$(MOUNT_DIR)/platform/sdl
SDLHDIR=$(MOUNT_DIR)/contrib/libsdl/include/SDL2

CMDIR=$(MOUNT_DIR)/engine/core
QCOMMONDIR=$(MOUNT_DIR)/engine/common
FSDIR=$(MOUNT_DIR)/engine/filesystem
NETDIR=$(MOUNT_DIR)/engine/network
COLDIR=$(MOUNT_DIR)/engine/collision
VMDIR=$(MOUNT_DIR)/engine/vm
AUDIODIR=$(MOUNT_DIR)/engine/audio
AIDIR=$(MOUNT_DIR)/engine/ai
UDIR=$(MOUNT_DIR)/platform/unix
W32DIR=$(MOUNT_DIR)/platform/windows
BLIBDIR=$(MOUNT_DIR)/engine/ai
JPDIR=$(MOUNT_DIR)/contrib/libjpeg
OGGDIR=$(MOUNT_DIR)/contrib/libogg
VORBISDIR=$(MOUNT_DIR)/contrib/libvorbis

bin_path=$(shell which $(1) 2> /dev/null)

STRIP ?= strip
PKG_CONFIG ?= pkg-config
INSTALL=install
MKDIR=mkdir -p

ifneq ($(call bin_path, $(PKG_CONFIG)),)
  ifneq ($(USE_SDL),0)
    SDL_INCLUDE ?= $(shell $(PKG_CONFIG) --silence-errors --cflags-only-I sdl2)
    SDL_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs sdl2)
  else
    X11_INCLUDE ?= $(shell $(PKG_CONFIG) --silence-errors --cflags-only-I x11)
    X11_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs x11)
  endif
  ifeq ($(USE_SYSTEM_OGG),1)
    OGG_CFLAGS ?= $(shell $(PKG_CONFIG) --silence-errors --cflags ogg || true)
    OGG_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs ogg || echo -logg)
  endif
  ifeq ($(USE_SYSTEM_VORBIS),1)
    VORBIS_CFLAGS ?= $(shell $(PKG_CONFIG) --silence-errors --cflags vorbisfile || true)
    VORBIS_LIBS ?= $(shell $(PKG_CONFIG) --silence-errors --libs vorbisfile || echo -lvorbisfile)
  endif
endif

# supply some reasonable defaults for SDL/X11
ifeq ($(X11_INCLUDE),)
  X11_INCLUDE = -I/usr/X11R6/include
endif
ifeq ($(X11_LIBS),)
  X11_LIBS = -lX11
endif
ifeq ($(SDL_LIBS),)
  SDL_LIBS = -lSDL2
endif

# supply some reasonable defaults for ogg/vorbis
ifeq ($(OGG_FLAGS),)
  OGG_FLAGS = -I$(OGGDIR)/include
endif
ifeq ($(VORBIS_FLAGS),)
  VORBIS_FLAGS = -I$(VORBISDIR)/include -I$(VORBISDIR)/lib
endif
ifeq ($(USE_SYSTEM_OGG),1)
  ifeq ($(OGG_LIBS),)
    OGG_LIBS = -logg
  endif
endif
ifeq ($(USE_SYSTEM_VORBIS),1)
  ifeq ($(VORBIS_LIBS),)
    VORBIS_LIBS = -lvorbisfile
  endif
endif

# extract version info
ifneq ($(COMPILE_PLATFORM),darwin)
VERSION=$(shell grep ".\+define[ \t]\+Q3_VERSION[ \t]\+\+" $(QCOMMONDIR)/q_shared.h | \
  sed -e 's/.*".* \([^ ]*\)"/\1/')
else
VERSION=1.32e
endif

# common qvm definition
ifeq ($(ARCH),x86_64)
  HAVE_VM_COMPILED = true
else
ifeq ($(ARCH),x86)
  HAVE_VM_COMPILED = true
else
  HAVE_VM_COMPILED = false
endif
endif

ifeq ($(ARCH),arm)
  HAVE_VM_COMPILED = true
endif
ifeq ($(ARCH),aarch64)
  HAVE_VM_COMPILED = true
endif

BASE_CFLAGS =

ifeq ($(USE_SYSTEM_JPEG),1)
  BASE_CFLAGS += -DUSE_SYSTEM_JPEG
endif

ifneq ($(HAVE_VM_COMPILED),true)
  BASE_CFLAGS += -DNO_VM_COMPILED
endif

ifneq ($(USE_RENDERER_DLOPEN),0)
  BASE_CFLAGS += -DUSE_RENDERER_DLOPEN
  BASE_CFLAGS += -DRENDERER_PREFIX=\\\"$(RENDERER_PREFIX)\\\"
  BASE_CFLAGS += -DRENDERER_DEFAULT="$(RENDERER_DEFAULT)"
endif

ifdef DEFAULT_BASEDIR
  BASE_CFLAGS += -DDEFAULT_BASEDIR=\\\"$(DEFAULT_BASEDIR)\\\"
endif

ifeq ($(USE_LOCAL_HEADERS),1)
  BASE_CFLAGS += -DUSE_LOCAL_HEADERS=1
endif

ifeq ($(USE_CURL),1)
  BASE_CFLAGS += -DUSE_CURL
  ifeq ($(USE_CURL_DLOPEN),1)
    BASE_CFLAGS += -DUSE_CURL_DLOPEN
  else
    ifeq ($(MINGW),1)
      BASE_CFLAGS += -DCURL_STATICLIB
    endif
  endif
endif

ifeq ($(USE_VULKAN_API),1)
  BASE_CFLAGS += -DUSE_VULKAN_API
endif

ifeq ($(USE_OPENGL_API),1)
  BASE_CFLAGS += -DUSE_OPENGL_API
endif

ifeq ($(GENERATE_DEPENDENCIES),1)
  BASE_CFLAGS += -MMD
endif


ARCHEXT=

CLIENT_EXTRA_FILES=


#############################################################################
# SETUP AND BUILD -- MINGW32
#############################################################################

ifdef MINGW

  ifeq ($(CROSS_COMPILING),1)
    # If CC is already set to something generic, we probably want to use
    # something more specific
    ifneq ($(findstring $(strip $(CC)),cc gcc),)
      CC=
    endif

    # We need to figure out the correct gcc and windres
    ifeq ($(ARCH),x86_64)
      MINGW_PREFIXES=x86_64-w64-mingw32 amd64-mingw32msvc
      STRIP=x86_64-w64-mingw32-strip
    endif
    ifeq ($(ARCH),x86)
      MINGW_PREFIXES=i686-w64-mingw32 i586-mingw32msvc i686-pc-mingw32
    endif

    ifndef CC
      CC=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-gcc))))
    endif

#   STRIP=$(MINGW_PREFIX)-strip -g

    ifndef WINDRES
      WINDRES=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
         $(call bin_path, $(MINGW_PREFIX)-windres))))
    endif
  else
    # Some MinGW installations define CC to cc, but don't actually provide cc,
    # so check that CC points to a real binary and use gcc if it doesn't
    ifeq ($(call bin_path, $(CC)),)
      CC=gcc
    endif

  endif

  # using generic windres if specific one is not present
  ifeq ($(WINDRES),)
    WINDRES=windres
  endif

  ifeq ($(CC),)
    $(error Cannot find a suitable cross compiler for $(PLATFORM))
  endif

  BASE_CFLAGS += -Wall -Wimplicit -Wstrict-prototypes -DUSE_ICON -DMINGW=1

  BASE_CFLAGS += -Wno-unused-result -fvisibility=hidden
  BASE_CFLAGS += -ffunction-sections -flto

  ifeq ($(ARCH),x86_64)
    ARCHEXT = .x64
    BASE_CFLAGS += -m64
    OPTIMIZE = -O2 -ffast-math
  endif
  ifeq ($(ARCH),x86)
    BASE_CFLAGS += -m32
    OPTIMIZE = -O2 -march=i586 -mtune=i686 -ffast-math
  endif

  SHLIBEXT = dll
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -shared $(LDFLAGS)

  BINEXT = .exe

  LDFLAGS += -mwindows -Wl,--dynamicbase -Wl,--nxcompat
  LDFLAGS += -Wl,--gc-sections -fvisibility=hidden
  LDFLAGS += -lwsock32 -lgdi32 -lwinmm -lole32 -lws2_32 -lpsapi -lcomctl32
  LDFLAGS += -flto

  CLIENT_LDFLAGS=$(LDFLAGS)

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += -DUSE_LOCAL_HEADERS=1 -I$(SDLHDIR)
    #CLIENT_CFLAGS += -DUSE_LOCAL_HEADERS=1
    ifeq ($(ARCH),x86)
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/contrib/libsdl/windows/mingw/lib32
      CLIENT_LDFLAGS += -lSDL2
      CLIENT_EXTRA_FILES += $(MOUNT_DIR)/contrib/libsdl/windows/mingw/lib32/SDL2.dll
    else
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/contrib/libsdl/windows/mingw/lib64
      CLIENT_LDFLAGS += -lSDL264
      CLIENT_EXTRA_FILES += $(MOUNT_DIR)/contrib/libsdl/windows/mingw/lib64/SDL264.dll
    endif
  endif

  ifeq ($(USE_CURL),1)
    BASE_CFLAGS += -I$(MOUNT_DIR)/contrib/libcurl/include
    ifeq ($(ARCH),x86)
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/contrib/libcurl/windows/mingw/lib32
    else
      CLIENT_LDFLAGS += -L$(MOUNT_DIR)/contrib/libcurl/windows/mingw/lib64
    endif
    CLIENT_LDFLAGS += -lcurl -lwldap32 -lcrypt32
  endif

  ifeq ($(USE_OGG_VORBIS),1)
    BASE_CFLAGS += -DUSE_OGG_VORBIS $(OGG_FLAGS) $(VORBIS_FLAGS)
    CLIENT_LDFLAGS += $(OGG_LIBS) $(VORBIS_LIBS)
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

else # !MINGW

ifeq ($(COMPILE_PLATFORM),darwin)

#############################################################################
# SETUP AND BUILD -- MACOS
#############################################################################

  BASE_CFLAGS += -Wall -Wimplicit -Wstrict-prototypes -pipe

  BASE_CFLAGS += -Wno-unused-result

  BASE_CFLAGS += -DMACOS_X

  OPTIMIZE = -O2 -fvisibility=hidden

  SHLIBEXT = dylib
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -dynamiclib $(LDFLAGS)

  ARCHEXT = .$(ARCH)

  LDFLAGS +=

  ifeq ($(ARCH),x86_64)
    BASE_CFLAGS += -arch x86_64
    LDFLAGS += -arch x86_64
  endif
  ifeq ($(ARCH),aarch64)
    BASE_CFLAGS += -arch arm64
    LDFLAGS += -arch arm64
  endif

  ifeq ($(USE_LOCAL_HEADERS),1)
    MACLIBSDIR=$(MOUNT_DIR)/contrib/libsdl/macosx
    BASE_CFLAGS += -I$(SDLHDIR)
    CLIENT_LDFLAGS += $(MACLIBSDIR)/libSDL2-2.0.0.dylib
    CLIENT_EXTRA_FILES += $(MACLIBSDIR)/libSDL2-2.0.0.dylib
  else
  ifneq ($(SDL_INCLUDE),)
    BASE_CFLAGS += $(SDL_INCLUDE)
    CLIENT_LDFLAGS = $(SDL_LIBS)
  else
    BASE_CFLAGS += -I/Library/Frameworks/SDL2.framework/Headers
    CLIENT_LDFLAGS += -F/Library/Frameworks -framework SDL2
  endif
  endif

  ifeq ($(USE_SYSTEM_JPEG),1)
    CLIENT_LDFLAGS += -ljpeg
  endif

  ifeq ($(USE_OGG_VORBIS),1)
    BASE_CFLAGS += -DUSE_OGG_VORBIS $(OGG_FLAGS) $(VORBIS_FLAGS)
    CLIENT_LDFLAGS += $(OGG_LIBS) $(VORBIS_LIBS)
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

else

#############################################################################
# SETUP AND BUILD -- *NIX PLATFORMS
#############################################################################

  BASE_CFLAGS += -Wall -Wimplicit -Wstrict-prototypes -pipe

  BASE_CFLAGS += -Wno-unused-result

  BASE_CFLAGS += -DUSE_ICON

  BASE_CFLAGS += -I/usr/include -I/usr/local/include

  OPTIMIZE = -O2 -fvisibility=hidden

  ifeq ($(ARCH),x86_64)
    ARCHEXT = .x64
  else
  ifeq ($(ARCH),x86)
    OPTIMIZE += -march=i586 -mtune=i686
  endif
  endif

  ifeq ($(ARCH),arm)
    OPTIMIZE += -march=armv7-a
    ARCHEXT = .arm
  endif

  ifeq ($(ARCH),aarch64)
    ARCHEXT = .aarch64
  endif

  SHLIBEXT = so
  SHLIBCFLAGS = -fPIC -fvisibility=hidden
  SHLIBLDFLAGS = -shared $(LDFLAGS)

  LDFLAGS += -lm
  LDFLAGS += -Wl,--gc-sections -fvisibility=hidden

  ifeq ($(USE_SDL),1)
    BASE_CFLAGS += $(SDL_INCLUDE)
    CLIENT_LDFLAGS = $(SDL_LIBS)
  else
    BASE_CFLAGS += $(X11_INCLUDE)
    CLIENT_LDFLAGS = $(X11_LIBS)
  endif

  ifeq ($(USE_SYSTEM_JPEG),1)
    CLIENT_LDFLAGS += -ljpeg
  endif

  ifeq ($(USE_CURL),1)
    ifeq ($(USE_CURL_DLOPEN),0)
      CLIENT_LDFLAGS += -lcurl
    endif
  endif

  ifeq ($(USE_OGG_VORBIS),1)
    BASE_CFLAGS += -DUSE_OGG_VORBIS $(OGG_FLAGS) $(VORBIS_FLAGS)
    CLIENT_LDFLAGS += $(OGG_LIBS) $(VORBIS_LIBS)
  endif

  ifeq ($(PLATFORM),linux)
    LDFLAGS += -ldl -Wl,--hash-style=both
    ifeq ($(ARCH),x86)
      # linux32 make ...
      BASE_CFLAGS += -m32
      LDFLAGS += -m32
    endif
  endif

  DEBUG_CFLAGS = $(BASE_CFLAGS) -DDEBUG -D_DEBUG -g -O0
  RELEASE_CFLAGS = $(BASE_CFLAGS) -DNDEBUG $(OPTIMIZE)

  DEBUG_LDFLAGS = -rdynamic

endif # *NIX platforms

endif # !MINGW


TARGET_CLIENT = $(CNAME)$(ARCHEXT)$(BINEXT)

TARGET_RENDV = $(RENDERER_PREFIX)_vulkan_$(SHLIBNAME)

TARGET_SERVER = $(DNAME)$(ARCHEXT)$(BINEXT)


TARGETS =

ifneq ($(BUILD_SERVER),0)
  TARGETS += $(B)/$(TARGET_SERVER)
endif

ifneq ($(BUILD_CLIENT),0)
  TARGETS += $(B)/$(TARGET_CLIENT)
  ifneq ($(USE_RENDERER_DLOPEN),0)
    ifeq ($(USE_VULKAN),1)
      TARGETS += $(B)/$(TARGET_RENDV)
    endif
  endif
endif

ifeq ($(USE_CCACHE),1)
  CC := ccache $(CC)
endif

ifneq ($(USE_RENDERER_DLOPEN),0)
    RENDCFLAGS=$(SHLIBCFLAGS)
else
    RENDCFLAGS=$(NOTSHLIBCFLAGS)
endif

define DO_CC
$(echo_cmd) "CC $<"
@$(MKDIR) $(dir $@)
$(Q)$(CC) $(CFLAGS) -o $@ -c $<
endef

define DO_REND_CC
$(echo_cmd) "REND_CC $<"
@$(MKDIR) $(dir $@)
$(Q)$(CC) $(CFLAGS) $(RENDCFLAGS) -o $@ -c $<
endef

define DO_REF_STR
$(echo_cmd) "REF_STR $<"
$(Q)rm -f $@
$(Q)$(STRINGIFY) $< $@
endef

define DO_BOT_CC
$(echo_cmd) "BOT_CC $<"
@$(MKDIR) $(dir $@)
$(Q)$(CC) $(CFLAGS) $(BOTCFLAGS) -DBOTLIB -o $@ -c $<
endef

define DO_AS
$(echo_cmd) "AS $<"
@$(MKDIR) $(dir $@)
$(Q)$(CC) $(CFLAGS) -DELF -x assembler-with-cpp -o $@ -c $<
endef

define DO_DED_CC
$(echo_cmd) "DED_CC $<"
@$(MKDIR) $(dir $@)
$(Q)$(CC) $(CFLAGS) -DDEDICATED -o $@ -c $<
endef

define DO_WINDRES
$(echo_cmd) "WINDRES $<"
@$(MKDIR) $(dir $@)
$(Q)$(WINDRES) -i $< -o $@
endef

ifndef SHLIBNAME
  SHLIBNAME=$(ARCH).$(SHLIBEXT)
endif

#############################################################################
# MAIN TARGETS
#############################################################################

default: release
all: debug release

debug:
	@$(MAKE) targets B=$(BD) CFLAGS="$(CFLAGS) $(DEBUG_CFLAGS)" LDFLAGS="$(LDFLAGS) $(DEBUG_LDFLAGS)" V=$(V)

release:
	@$(MAKE) targets B=$(BR) CFLAGS="$(CFLAGS) $(RELEASE_CFLAGS)" V=$(V)

define ADD_COPY_TARGET
TARGETS += $2
$2: $1
	$(echo_cmd) "CP $$<"
	@cp $1 $2
endef

# These functions allow us to generate rules for copying a list of files
# into the base directory of the build; this is useful for bundling libs,
# README files or whatever else
define GENERATE_COPY_TARGETS
$(foreach FILE,$1, \
  $(eval $(call ADD_COPY_TARGET, \
    $(FILE), \
    $(addprefix $(B)/,$(notdir $(FILE))))))
endef

ifneq ($(BUILD_CLIENT),0)
  $(call GENERATE_COPY_TARGETS,$(CLIENT_EXTRA_FILES))
endif

# Create the build directories and tools, print out
# an informational message, then start building
targets: makedirs tools
	@echo ""
	@echo "Building quake3 in $(B):"
	@echo ""
	@echo "  VERSION: $(VERSION)"
	@echo "  PLATFORM: $(PLATFORM)"
	@echo "  ARCH: $(ARCH)"
	@echo "  COMPILE_PLATFORM: $(COMPILE_PLATFORM)"
	@echo "  COMPILE_ARCH: $(COMPILE_ARCH)"
ifdef MINGW
	@echo "  WINDRES: $(WINDRES)"
endif
	@echo "  CC: $(CC)"
	@echo ""
	@echo "  CFLAGS:"
	@for i in $(CFLAGS); \
	do \
		echo "    $$i"; \
	done
	@echo ""
	@echo "  Output:"
	@for i in $(TARGETS); \
	do \
		echo "    $$i"; \
	done
	@echo ""
ifneq ($(TARGETS),)
	@$(MAKE) $(TARGETS) V=$(V)
endif

makedirs:
	@if [ ! -d $(BUILD_DIR) ];then $(MKDIR) $(BUILD_DIR);fi
	@if [ ! -d $(B) ];then $(MKDIR) $(B);fi
	@if [ ! -d $(B)/client ];then $(MKDIR) $(B)/client;fi
	@if [ ! -d $(B)/client/jpeg ];then $(MKDIR) $(B)/client/jpeg;fi
ifeq ($(USE_SYSTEM_OGG),0)
	@if [ ! -d $(B)/client/ogg ];then $(MKDIR) $(B)/client/ogg;fi
endif
ifeq ($(USE_SYSTEM_VORBIS),0)
	@if [ ! -d $(B)/client/vorbis ];then $(MKDIR) $(B)/client/vorbis;fi
endif
	@if [ ! -d $(B)/rendv ];then $(MKDIR) $(B)/rendv;fi
ifneq ($(BUILD_SERVER),0)
	@if [ ! -d $(B)/ded ];then $(MKDIR) $(B)/ded;fi
endif

#############################################################################
# CLIENT/SERVER
#############################################################################

Q3RENDVOBJ = \
  $(B)/rendv/tr_animation.o \
  $(B)/rendv/tr_backend.o \
  $(B)/rendv/threading/tr_backend_thread.o \
  $(B)/rendv/world/tr_bsp.o \
  $(B)/rendv/tr_cmdbuf.o \
  $(B)/rendv/tr_cmds.o \
  $(B)/rendv/tr_curve.o \
  $(B)/rendv/materials/tr_expression.o \
  $(B)/rendv/text/tr_font.o \
  $(B)/rendv/images/tr_image.o \
  $(B)/rendv/images/tr_image_png.o \
  $(B)/rendv/images/tr_image_jpg.o \
  $(B)/rendv/images/tr_image_bmp.o \
  $(B)/rendv/images/tr_image_tga.o \
  $(B)/rendv/images/tr_image_pcx.o \
  $(B)/rendv/tr_init.o \
  $(B)/rendv/lighting/tr_light.o \
  $(B)/rendv/tr_main.o \
  $(B)/rendv/effects/tr_marks.o \
  $(B)/rendv/materials/tr_material.o \
  $(B)/rendv/materials/tr_material_opt.o \
  $(B)/rendv/tr_mesh.o \
  $(B)/rendv/models/tr_model.o \
  $(B)/rendv/models/tr_model_iqm.o \
  $(B)/rendv/tr_noise.o \
  $(B)/rendv/tr_scene.o \
  $(B)/rendv/sorting/tr_sort.o \
  $(B)/rendv/shading/tr_shade.o \
  $(B)/rendv/shading/tr_shade_calc.o \
  $(B)/rendv/shading/tr_shader.o \
  $(B)/rendv/materials/tr_shader_compat.o \
  $(B)/rendv/world/tr_sky.o \
  $(B)/rendv/tr_surface.o \
  $(B)/rendv/threading/tr_sync.o \
  $(B)/rendv/world/tr_world.o \
  $(B)/rendv/vulkan/vk.o \
  $(B)/rendv/vulkan/vk_flares.o \
  $(B)/rendv/vulkan/vk_vbo.o \
  $(B)/rendv/vulkan/vk_uber.o \
  $(B)/rendv/vulkan/vk_descriptors.o \

ifneq ($(USE_RENDERER_DLOPEN), 0)
  Q3RENDVOBJ += \
    $(B)/rendv/common/q_shared.o \
    $(B)/rendv/common/compression/puff.o \
    $(B)/rendv/common/math/q_math.o
endif

JPGOBJ = \
  $(B)/client/jpeg/jaricom.o \
  $(B)/client/jpeg/jcapimin.o \
  $(B)/client/jpeg/jcapistd.o \
  $(B)/client/jpeg/jcarith.o \
  $(B)/client/jpeg/jccoefct.o  \
  $(B)/client/jpeg/jccolor.o \
  $(B)/client/jpeg/jcdctmgr.o \
  $(B)/client/jpeg/jchuff.o   \
  $(B)/client/jpeg/jcinit.o \
  $(B)/client/jpeg/jcmainct.o \
  $(B)/client/jpeg/jcmarker.o \
  $(B)/client/jpeg/jcmaster.o \
  $(B)/client/jpeg/jcomapi.o \
  $(B)/client/jpeg/jcparam.o \
  $(B)/client/jpeg/jcprepct.o \
  $(B)/client/jpeg/jcsample.o \
  $(B)/client/jpeg/jctrans.o \
  $(B)/client/jpeg/jdapimin.o \
  $(B)/client/jpeg/jdapistd.o \
  $(B)/client/jpeg/jdarith.o \
  $(B)/client/jpeg/jdatadst.o \
  $(B)/client/jpeg/jdatasrc.o \
  $(B)/client/jpeg/jdcoefct.o \
  $(B)/client/jpeg/jdcolor.o \
  $(B)/client/jpeg/jddctmgr.o \
  $(B)/client/jpeg/jdhuff.o \
  $(B)/client/jpeg/jdinput.o \
  $(B)/client/jpeg/jdmainct.o \
  $(B)/client/jpeg/jdmarker.o \
  $(B)/client/jpeg/jdmaster.o \
  $(B)/client/jpeg/jdmerge.o \
  $(B)/client/jpeg/jdpostct.o \
  $(B)/client/jpeg/jdsample.o \
  $(B)/client/jpeg/jdtrans.o \
  $(B)/client/jpeg/jerror.o \
  $(B)/client/jpeg/jfdctflt.o \
  $(B)/client/jpeg/jfdctfst.o \
  $(B)/client/jpeg/jfdctint.o \
  $(B)/client/jpeg/jidctflt.o \
  $(B)/client/jpeg/jidctfst.o \
  $(B)/client/jpeg/jidctint.o \
  $(B)/client/jpeg/jmemmgr.o \
  $(B)/client/jpeg/jmemnobs.o \
  $(B)/client/jpeg/jquant1.o \
  $(B)/client/jpeg/jquant2.o \
  $(B)/client/jpeg/jutils.o

ifeq ($(USE_OGG_VORBIS),1)
ifeq ($(USE_SYSTEM_OGG),0)
OGGOBJ = \
  $(B)/client/ogg/bitwise.o \
  $(B)/client/ogg/framing.o
endif

ifeq ($(USE_SYSTEM_VORBIS),0)
VORBISOBJ = \
  $(B)/client/vorbis/analysis.o \
  $(B)/client/vorbis/bitrate.o \
  $(B)/client/vorbis/block.o \
  $(B)/client/vorbis/codebook.o \
  $(B)/client/vorbis/envelope.o \
  $(B)/client/vorbis/floor0.o \
  $(B)/client/vorbis/floor1.o \
  $(B)/client/vorbis/info.o \
  $(B)/client/vorbis/lookup.o \
  $(B)/client/vorbis/lpc.o \
  $(B)/client/vorbis/lsp.o \
  $(B)/client/vorbis/mapping0.o \
  $(B)/client/vorbis/mdct.o \
  $(B)/client/vorbis/psy.o \
  $(B)/client/vorbis/registry.o \
  $(B)/client/vorbis/res0.o \
  $(B)/client/vorbis/smallft.o \
  $(B)/client/vorbis/sharedbook.o \
  $(B)/client/vorbis/synthesis.o \
  $(B)/client/vorbis/vorbisfile.o \
  $(B)/client/vorbis/window.o
endif
endif

Q3OBJ = \
  $(B)/client/cgame_interface.o \
  $(B)/client/media/cl_cin.o \
  $(B)/client/ui/cl_console.o \
  $(B)/client/input/cl_input.o \
  $(B)/client/input/cl_keys.o \
  $(B)/client/cl_main.o \
  $(B)/client/network/cl_net_chan.o \
  $(B)/client/network/cl_parse.o \
  $(B)/client/ui/cl_scrn.o \
  $(B)/client/ui_interface.o \
  $(B)/client/media/cl_avi.o \
  $(B)/client/media/cl_jpeg.o \
  \
  $(B)/client/collision/cm_load.o \
  $(B)/client/collision/cm_patch.o \
  $(B)/client/collision/cm_polylib.o \
  $(B)/client/collision/cm_test.o \
  $(B)/client/collision/cm_trace.o \
  \
  $(B)/client/core/cmd.o \
  $(B)/client/core/common.o \
  $(B)/client/core/cvar.o \
  $(B)/client/filesystem/files.o \
  $(B)/client/core/history.o \
  $(B)/client/core/keys.o \
  $(B)/client/common/crypto/md4.o \
  $(B)/client/common/crypto/md5.o \
  $(B)/client/network/msg.o \
  $(B)/client/network/net_chan.o \
  $(B)/client/network/net_ip.o \
  $(B)/client/common/compression/huffman.o \
  $(B)/client/common/compression/huffman_static.o \
  \
  $(B)/client/audio/codecs/snd_adpcm.o \
  $(B)/client/audio/snd_dma.o \
  $(B)/client/audio/snd_mem.o \
  $(B)/client/audio/snd_mix.o \
  $(B)/client/audio/codecs/snd_wavelet.o \
  \
  $(B)/client/audio/snd_main.o \
  $(B)/client/audio/codecs/snd_codec.o \
  $(B)/client/audio/codecs/snd_codec_wav.o \
  \
  $(B)/client/server/sv_bot.o \
  $(B)/client/server/sv_ccmds.o \
  $(B)/client/server/sv_client.o \
  $(B)/client/server/network/sv_filter.o \
  $(B)/client/server/sv_game.o \
  $(B)/client/server/sv_init.o \
  $(B)/client/server/sv_main.o \
  $(B)/client/server/network/sv_net_chan.o \
  $(B)/client/server/sv_snapshot.o \
  $(B)/client/server/world/sv_world.o \
  \
  $(B)/client/common/math/q_math.o \
  $(B)/client/common/q_shared.o \
  \
  $(B)/client/filesystem/unzip.o \
  $(B)/client/common/compression/puff.o \
  $(B)/client/vm/vm.o \
  $(B)/client/vm/vm_interpreted.o \
  $(B)/client/ai/ai_interface.o \
  $(B)/client/ai/util/crc.o \
  $(B)/client/ai/util/libvar.o \
  $(B)/client/ai/util/log.o \
  $(B)/client/ai/util/memory.o \
  $(B)/client/ai/util/precomp.o \
  $(B)/client/ai/util/script.o \
  $(B)/client/ai/util/struct.o

ifneq ($(USE_SYSTEM_JPEG),1)
  Q3OBJ += $(JPGOBJ)
endif

ifeq ($(USE_OGG_VORBIS),1)
  Q3OBJ += $(OGGOBJ) $(VORBISOBJ) \
    $(B)/client/audio/codecs/snd_codec_ogg.o
endif

ifneq ($(USE_RENDERER_DLOPEN),1)
  ifeq ($(USE_VULKAN),1)
    Q3OBJ += $(Q3RENDVOBJ)
  endif
endif

ifeq ($(ARCH),x86)
ifndef MINGW
  Q3OBJ += \
    $(B)/client/audio/asm/snd_mix_mmx.o \
    $(B)/client/audio/asm/snd_mix_sse.o
endif
endif

ifeq ($(ARCH),x86_64)
  Q3OBJ += \
    $(B)/client/audio/asm/snd_mix_x86_64.o
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),x86)
    Q3OBJ += $(B)/client/vm/vm_x86.o
  endif
  ifeq ($(ARCH),x86_64)
    Q3OBJ += $(B)/client/vm/vm_x86.o
  endif
  ifeq ($(ARCH),arm)
    Q3OBJ += $(B)/client/vm/vm_armv7l.o
  endif
  ifeq ($(ARCH),aarch64)
    Q3OBJ += $(B)/client/vm/vm_aarch64.o
  endif
endif

ifeq ($(USE_CURL),1)
  Q3OBJ += $(B)/client/network/cl_curl.o
endif

ifdef MINGW

  Q3OBJ += \
    $(B)/client/windows/win_main.o \
    $(B)/client/windows/win_shared.o \
    $(B)/client/windows/win_syscon.o \
    $(B)/client/windows/win_resource.o

ifeq ($(USE_SDL),1)
    Q3OBJ += \
        $(B)/client/sdl/sdl_glimp.o \
        $(B)/client/sdl/sdl_gamma.o \
        $(B)/client/sdl/sdl_input.o \
        $(B)/client/sdl/sdl_snd.o
else # !USE_SDL
    Q3OBJ += \
        $(B)/client/windows/win_gamma.o \
        $(B)/client/windows/win_glimp.o \
        $(B)/client/windows/win_input.o \
        $(B)/client/windows/win_minimize.o \
        $(B)/client/windows/win_snd.o \
        $(B)/client/windows/win_wndproc.o

ifeq ($(USE_OPENGL_API),1)
    Q3OBJ += \
        $(B)/client/windows/win_qgl.o
endif

ifeq ($(USE_VULKAN_API),1)
    Q3OBJ += \
        $(B)/client/windows/win_qvk.o
endif
endif # !USE_SDL

else # !MINGW

  Q3OBJ += \
    $(B)/client/unix/unix_main.o \
    $(B)/client/unix/unix_shared.o \
    $(B)/client/unix/linux_signals.o

ifeq ($(USE_SDL),1)
    Q3OBJ += \
        $(B)/client/sdl/sdl_glimp.o \
        $(B)/client/sdl/sdl_gamma.o \
        $(B)/client/sdl/sdl_input.o \
        $(B)/client/sdl/sdl_snd.o
else # !USE_SDL
    Q3OBJ += \
        $(B)/client/unix/linux_glimp.o \
        $(B)/client/unix/linux_snd.o \
        $(B)/client/unix/x11_dga.o \
        $(B)/client/unix/x11_randr.o \
        $(B)/client/unix/x11_vidmode.o
ifeq ($(USE_OPENGL_API),1)
    Q3OBJ += \
        $(B)/client/unix/linux_qgl.o
endif
ifeq ($(USE_VULKAN_API),1)
    Q3OBJ += \
        $(B)/client/unix/linux_qvk.o
endif
endif # !USE_SDL

endif # !MINGW

# client binary

$(B)/$(TARGET_CLIENT): $(Q3OBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3OBJ) $(CLIENT_LDFLAGS) \
		$(LDFLAGS)

# modular renderers

$(B)/$(TARGET_RENDV): $(Q3RENDVOBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3RENDVOBJ) $(SHLIBCFLAGS) $(SHLIBLDFLAGS)

#############################################################################
# DEDICATED SERVER
#############################################################################

Q3DOBJ = \
  $(B)/ded/server/sv_bot.o \
  $(B)/ded/server/sv_client.o \
  $(B)/ded/server/sv_ccmds.o \
  $(B)/ded/server/network/sv_filter.o \
  $(B)/ded/server/sv_game.o \
  $(B)/ded/server/sv_init.o \
  $(B)/ded/server/sv_main.o \
  $(B)/ded/server/network/sv_net_chan.o \
  $(B)/ded/server/sv_snapshot.o \
  $(B)/ded/server/world/sv_world.o \
  \
  $(B)/ded/collision/cm_load.o \
  $(B)/ded/collision/cm_patch.o \
  $(B)/ded/collision/cm_polylib.o \
  $(B)/ded/collision/cm_test.o \
  $(B)/ded/collision/cm_trace.o \
  $(B)/ded/core/cmd.o \
  $(B)/ded/core/common.o \
  $(B)/ded/core/cvar.o \
  $(B)/ded/filesystem/files.o \
  $(B)/ded/core/history.o \
  $(B)/ded/core/keys.o \
  $(B)/ded/common/crypto/md4.o \
  $(B)/ded/common/crypto/md5.o \
  $(B)/ded/network/msg.o \
  $(B)/ded/network/net_chan.o \
  $(B)/ded/network/net_ip.o \
  $(B)/ded/common/compression/huffman.o \
  $(B)/ded/common/compression/huffman_static.o \
  \
  $(B)/ded/common/math/q_math.o \
  $(B)/ded/common/q_shared.o \
  \
  $(B)/ded/filesystem/unzip.o \
  $(B)/ded/vm/vm.o \
  $(B)/ded/vm/vm_interpreted.o \
  $(B)/ded/ai/ai_interface.o \
  $(B)/ded/ai/util/crc.o \
  $(B)/ded/ai/util/libvar.o \
  $(B)/ded/ai/util/log.o \
  $(B)/ded/ai/util/memory.o \
  $(B)/ded/ai/util/precomp.o \
  $(B)/ded/ai/util/script.o \
  $(B)/ded/ai/util/struct.o

ifdef MINGW
  Q3DOBJ += \
  $(B)/ded/windows/win_main.o \
  $(B)/client/windows/win_resource.o \
  $(B)/ded/windows/win_shared.o \
  $(B)/ded/windows/win_syscon.o
else
  Q3DOBJ += \
  $(B)/ded/unix/linux_signals.o \
  $(B)/ded/unix/unix_main.o \
  $(B)/ded/unix/unix_shared.o
endif

ifeq ($(HAVE_VM_COMPILED),true)
  ifeq ($(ARCH),x86)
    Q3DOBJ += $(B)/ded/vm/vm_x86.o
  endif
  ifeq ($(ARCH),x86_64)
    Q3DOBJ += $(B)/ded/vm/vm_x86.o
  endif
  ifeq ($(ARCH),arm)
    Q3DOBJ += $(B)/ded/vm/vm_armv7l.o
  endif
  ifeq ($(ARCH),aarch64)
    Q3DOBJ += $(B)/ded/vm/vm_aarch64.o
  endif
endif

$(B)/$(TARGET_SERVER): $(Q3DOBJ)
	$(echo_cmd) "LD $@"
	$(Q)$(CC) -o $@ $(Q3DOBJ) $(LDFLAGS)

#############################################################################
## CLIENT/SERVER RULES
#############################################################################

$(B)/client/%.o: $(ADIR)/%.s
	$(DO_AS)

$(B)/client/%.o: $(CDIR)/%.c
	$(DO_CC)

$(B)/client/ui/%.o: $(CDIR)/ui/%.c
	$(DO_CC)

$(B)/client/input/%.o: $(CDIR)/input/%.c
	$(DO_CC)

$(B)/client/media/%.o: $(CDIR)/media/%.c
	$(DO_CC)

$(B)/client/network/%.o: $(CDIR)/network/%.c
	$(DO_CC)

$(B)/client/server/%.o: $(SDIR)/%.c
	$(DO_CC)

$(B)/client/server/network/%.o: $(SDIR)/network/%.c
	$(DO_CC)

$(B)/client/server/world/%.o: $(SDIR)/world/%.c
	$(DO_CC)

$(B)/client/core/%.o: $(CMDIR)/%.c
	$(DO_CC)

$(B)/client/common/%.o: $(QCOMMONDIR)/%.c
	$(DO_CC)

$(B)/client/common/math/%.o: $(QCOMMONDIR)/math/%.c
	$(DO_CC)

$(B)/client/common/compression/%.o: $(QCOMMONDIR)/compression/%.c
	$(DO_CC)

$(B)/client/common/crypto/%.o: $(QCOMMONDIR)/crypto/%.c
	$(DO_CC)

$(B)/client/filesystem/%.o: $(FSDIR)/%.c
	$(DO_CC)

$(B)/client/network/%.o: $(NETDIR)/%.c
	$(DO_CC)

$(B)/client/collision/%.o: $(COLDIR)/%.c
	$(DO_CC)

$(B)/client/vm/%.o: $(VMDIR)/%.c
	$(DO_CC)

$(B)/client/audio/%.o: $(AUDIODIR)/%.c
	$(DO_CC)

$(B)/client/audio/codecs/%.o: $(AUDIODIR)/codecs/%.c
	$(DO_CC)

$(B)/client/audio/asm/%.o: $(AUDIODIR)/asm/%.s
	$(DO_AS)

$(B)/client/ai/%.o: $(AIDIR)/%.c
	$(DO_BOT_CC)

$(B)/client/ai/navigation/%.o: $(AIDIR)/navigation/%.c
	$(DO_BOT_CC)

$(B)/client/ai/behavior/%.o: $(AIDIR)/behavior/%.c
	$(DO_BOT_CC)

$(B)/client/ai/util/%.o: $(AIDIR)/util/%.c
	$(DO_BOT_CC)

$(B)/client/jpeg/%.o: $(JPDIR)/%.c
	$(DO_CC)

$(B)/client/ogg/%.o: $(OGGDIR)/src/%.c
	$(DO_CC)

$(B)/client/vorbis/%.o: $(VORBISDIR)/lib/%.c
	$(DO_CC)

$(B)/client/sdl/%.o: $(SDLDIR)/%.c
	$(DO_CC)

$(B)/rendv/%.o: $(RDIR)/%.c
	$(DO_REND_CC)

$(B)/rendv/vulkan/%.o: $(RDIR)/vulkan/%.c
	$(DO_REND_CC)

$(B)/rendv/images/%.o: $(RDIR)/images/%.c
	$(DO_REND_CC)

$(B)/rendv/models/%.o: $(RDIR)/models/%.c
	$(DO_REND_CC)

$(B)/rendv/effects/%.o: $(RDIR)/effects/%.c
	$(DO_REND_CC)

$(B)/rendv/lighting/%.o: $(RDIR)/lighting/%.c
	$(DO_REND_CC)

$(B)/rendv/shading/%.o: $(RDIR)/shading/%.c
	$(DO_REND_CC)

$(B)/rendv/text/%.o: $(RDIR)/text/%.c
	$(DO_REND_CC)

$(B)/rendv/world/%.o: $(RDIR)/world/%.c
	$(DO_REND_CC)

$(B)/rendv/common/%.o: $(QCOMMONDIR)/%.c
	$(DO_REND_CC)

$(B)/rendv/common/math/%.o: $(QCOMMONDIR)/math/%.c
	$(DO_REND_CC)

$(B)/rendv/common/compression/%.o: $(QCOMMONDIR)/compression/%.c
	$(DO_REND_CC)

$(B)/client/unix/%.o: $(UDIR)/%.c
	$(DO_CC)

$(B)/client/windows/%.o: $(W32DIR)/%.c
	$(DO_CC)

$(B)/client/windows/%.o: $(W32DIR)/%.rc
	$(DO_WINDRES)

$(B)/ded/%.o: $(ADIR)/%.s
	$(DO_AS)

$(B)/ded/server/%.o: $(SDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/server/network/%.o: $(SDIR)/network/%.c
	$(DO_DED_CC)

$(B)/ded/server/world/%.o: $(SDIR)/world/%.c
	$(DO_DED_CC)

$(B)/ded/core/%.o: $(CMDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/common/%.o: $(QCOMMONDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/common/math/%.o: $(QCOMMONDIR)/math/%.c
	$(DO_DED_CC)

$(B)/ded/common/compression/%.o: $(QCOMMONDIR)/compression/%.c
	$(DO_DED_CC)

$(B)/ded/common/crypto/%.o: $(QCOMMONDIR)/crypto/%.c
	$(DO_DED_CC)

$(B)/ded/filesystem/%.o: $(FSDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/network/%.o: $(NETDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/collision/%.o: $(COLDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/vm/%.o: $(VMDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/ai/%.o: $(AIDIR)/%.c
	$(DO_BOT_CC)

$(B)/ded/ai/navigation/%.o: $(AIDIR)/navigation/%.c
	$(DO_BOT_CC)

$(B)/ded/ai/behavior/%.o: $(AIDIR)/behavior/%.c
	$(DO_BOT_CC)

$(B)/ded/ai/util/%.o: $(AIDIR)/util/%.c
	$(DO_BOT_CC)

$(B)/ded/unix/%.o: $(UDIR)/%.c
	$(DO_DED_CC)

$(B)/ded/windows/%.o: $(W32DIR)/%.c
	$(DO_DED_CC)

$(B)/ded/windows/%.o: $(W32DIR)/%.rc
	$(DO_WINDRES)

#############################################################################
# MISC
#############################################################################

install: release
	@for i in $(TARGETS); do \
		if [ -f $(BR)$$i ]; then \
			$(INSTALL) -D -m 0755 "$(BR)/$$i" "$(DESTDIR)$$i"; \
			$(STRIP) "$(DESTDIR)$$i"; \
		fi \
	done

clean: clean-debug clean-release

clean2:
	@echo "CLEAN $(B)"
	@if [ -d $(B) ];then (find $(B) -name '*.d' -exec rm {} \;)fi
	@rm -f $(Q3OBJ) $(Q3DOBJ)
	@rm -f $(TARGETS)

clean-debug:
	@rm -rf $(BD)

clean-release:
	@echo $(BR)
	@rm -rf $(BR)

distclean: clean
	@rm -rf $(BUILD_DIR)

#############################################################################
# DEPENDENCIES
#############################################################################

D_FILES=$(shell find . -name '*.d')

ifneq ($(strip $(D_FILES)),)
 include $(D_FILES)
endif

.PHONY: all clean clean2 clean-debug clean-release copyfiles \
	debug default dist distclean makedirs release \
	targets tools toolsclean
