# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# The PX-42F's own board definition (boards/px42f.h): RP2350A, 16MB W25Q128
# flash, no wireless chip.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD px42f CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(PX-42F-Plus C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
# Plus42's core catches std::bad_alloc (equations, directories, units).
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

pico_sdk_init()

# Intel Decimal Floating-Point Math Library (BID), providing authentic
# HP-42S decimal arithmetic. Built from the upstream sources in
# third_party/IntelRDFPMathLib20U1 via its own makefile, cross-compiled
# with the same toolchain and target architecture as the rest of this
# project. If PICO_BOARD ever changes to a non-Cortex-M33 target, update
# INTELDECIMAL_ARCH_FLAGS to match.
set(INTELDECIMAL_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/IntelRDFPMathLib20U1)
set(INTELDECIMAL_ARCH_FLAGS "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mcmse -mfloat-abi=softfp -Os -ffunction-sections -fdata-sections")
set(LIBBID_PATH ${INTELDECIMAL_DIR}/LIBRARY/libbid.a)

add_custom_command(
        OUTPUT ${LIBBID_PATH}
        COMMAND make -C ${INTELDECIMAL_DIR}/LIBRARY
                CC=${CMAKE_C_COMPILER}
                CC_NAME=gcc
                "AR_CMD=${CMAKE_AR} rv"
                IML_HOST_OS=LINUX
                "CFLAGS_OPT=${INTELDECIMAL_ARCH_FLAGS}"
                CALL_BY_REF=1 GLOBAL_RND=1 GLOBAL_FLAGS=1 UNCHANGED_BINARY_FLAGS=0
        COMMENT "Building Intel Decimal Floating-Point Library (libbid.a) for Cortex-M33"
        VERBATIM
)
add_custom_target(inteldecimal_lib DEPENDS ${LIBBID_PATH})

# Add executable. Default name is the project name, version 0.1

file(GLOB FREE42_CORE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/core/*.cc)

add_executable(PX-42F-Plus
        main.c
        display.c
        display_draw.c
        keyboard.c
        rtc.c
        ra8900.c
        rtc_format.c
        battery.c
        speaker.c
        sounds.c
        midiplay.c
        dutyshape.c
        wav.c
        sounds_data.c
        settings.c
        settings_menu.c
        menu_places.c
        splash.c
        images/splash_image.c
        flashdisk.c
        crash_report.c
        png1bit.c
        printer.c
        power.c
        ramstate.c
        cpuclock.c
        idle.c
        fatfs_diskio.c
        statefs.c
        usb_msc.c
        usb_descriptors.c
        usbprint.c
        escpos.c
        third_party/fatfs/ff.c
        third_party/fatfs/ffunicode.c
        fonts/font_10x16_data.c
        fonts/font_24x10_data.c
        fonts/font_8x12_data.c
        fonts/font_6x8_data.c
        fonts.c
        fontsheet.c
        pngwrite.c
        screenshot.c
        keyhold.c
        irtx.c
        hp82240.c
        app.c
        ui.c
        ui_screen.c
        px_draw.c
        px_layer.c
        bootlog.c
        appdec.c
        appfile.c
        soundname.c
        safefile.c
        timemodule.c
        alarms.c
        stopwatch.c
        alphakeys.c
        icons.c
        menu_icons.c
        ${FREE42_CORE_SOURCES}
        )

add_dependencies(PX-42F-Plus inteldecimal_lib)

# The top of RAM, in SRAM bank 1, stays powered through the deep power-off
# and holds what has to survive it -- the calculator's memory and the
# display's framebuffer (retained.h, RETAINED_BASE): keep the heap out.
pico_set_linker_script_var(PX-42F-Plus HEAP_LIMIT 0x20057000)

# The Intel decimal library's tables in flash, not RAM: ld/default_rodata_excludes.incl.
pico_add_linker_script_override_path(PX-42F-Plus ${CMAKE_CURRENT_LIST_DIR}/ld FILES default_rodata_excludes.incl)

pico_set_program_name(PX-42F-Plus "PX-42F-Plus")
pico_set_program_version(PX-42F-Plus "0.50b")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(PX-42F-Plus 0)
pico_enable_stdio_usb(PX-42F-Plus 1)

# Add the standard library to the build
target_link_libraries(PX-42F-Plus
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(PX-42F-Plus PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src/core
        ${CMAKE_CURRENT_LIST_DIR}/fonts
        ${CMAKE_CURRENT_LIST_DIR}/third_party/fatfs
)

# Flags required to match the ABI that bid_functions.h declares, and to
# match how libbid.a itself was built (see INTELDECIMAL_ARCH_FLAGS above).
# BCD_MATH switches the core from double-based "binary" math to the real
# BID128-backed Phloat class, for authentic HP-42S decimal arithmetic.
target_compile_definitions(PX-42F-Plus PRIVATE
        BCD_MATH
        BID_SIZE_LONG=4
        DECIMAL_CALL_BY_REFERENCE=1
        DECIMAL_GLOBAL_ROUNDING=1
        DECIMAL_GLOBAL_ROUNDING_ACCESS_FUNCTIONS=1
        DECIMAL_GLOBAL_EXCEPTION_FLAGS=1
        DECIMAL_GLOBAL_EXCEPTION_FLAGS_ACCESS_FUNCTIONS=1
        # Bumped from the SDK default (0x800) to the max that fits in
        # RP2350's dedicated 4KB SCRATCH_Y bank, where the default linker
        # script places the core0 stack. core0 barely does anything in this
        # project now (see main.c -- it just launches core1, which runs
        # everything, including all of Free42, with its own much larger
        # explicit stack in ordinary SRAM), so this is just cheap safety
        # margin, not the real fix for Free42's stack needs. Must be a
        # compile definition (not just a CMake variable) because crt0.S is
        # an INTERFACE source compiled as part of this target itself.
        PICO_STACK_SIZE=0x1000
        # PX-42 port hooks in src/core (my_fopen etc. -> statefs.c)
        PX42
        # Plus42 handles running out of memory itself -- malloc() returning
        # NULL, and std::bad_alloc from new (PICO_CXX_ENABLE_EXCEPTIONS) --
        # with "Insufficient Memory". The SDK's default panics instead,
        # which reboots the calculator.
        PICO_MALLOC_PANIC=0
        # The core voltage survives every reboot but a power-up -- and the
        # idle clock (cpuclock.c) leaves it at 1.00V. Have the SDK's startup
        # code put it back to 1.10V before it switches to 150MHz, or a wake
        # from OFF, a reflash or a crash reboot would run full speed
        # under-volted (it only does this by itself for RP2040 overclocks).
        SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
        SYS_CLK_VREG_VOLTAGE_MIN=VREG_VOLTAGE_1_10
        # This app provides its own TinyUSB config and descriptors
        # (tusb_config.h, usb_descriptors.c: serial console + state-file
        # drive + picotool reset). Linking tinyusb_device turns all of these
        # off by default; keep pico_stdio_usb running TinyUSB in its
        # background IRQ as before, and keep the reset-to-BOOTSEL support.
        # A console write waits this long for a computer that has the port
        # open but isn't reading -- the SDK's default, 500ms, outlasts the
        # 100ms watchdog (a frozen terminal + one printf = a reset).
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=50000
        PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=1
        PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1
        PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
        PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
        PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=1
        PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF=3
)

# Add any user requested libraries
# The restart-into-the-bootloader request goes through main.c first (see
# __wrap_rom_reset_usb_boot_extra there).
target_link_options(PX-42F-Plus PRIVATE -Wl,--wrap=rom_reset_usb_boot_extra)
# The USB thermal printer needs real bus resets that TinyUSB's RP2 host
# driver doesn't do (usbprint.c).
target_link_options(PX-42F-Plus PRIVATE -Wl,--wrap=hcd_port_reset -Wl,--wrap=hcd_port_reset_end
                    -Wl,--wrap=hcd_setup_send)
# Plus42's display_error(), C++ name: the error sound, without touching the
# core (__wrap__Z13display_errori in free42_app.cc).
target_link_options(PX-42F-Plus PRIVATE -Wl,--wrap=_Z13display_errori)

pico_generate_pio_header(PX-42F-Plus ${CMAKE_CURRENT_LIST_DIR}/irtx.pio)

target_link_libraries(PX-42F-Plus
        hardware_pio
        pico_multicore
        hardware_spi
        hardware_uart
        hardware_watchdog
        hardware_i2c
        hardware_powman
        hardware_adc
        hardware_pwm
        hardware_dma
        hardware_flash
        pico_flash
        tinyusb_device
        tinyusb_host
        ${LIBBID_PATH}
        )

pico_add_extra_outputs(PX-42F-Plus)

