"""How GIGABYTE's graphical setup front-end arranges the AMI setup forms.

The real BIOS keeps every setting in one big AMI IFR formset ("Setup"), but
the GIGABYTE GUI (CoolGUIDxe) shows its own tab layout and regroups items
from different forms.  The grouping is hard-coded inside that binary, so it
is recreated here from screenshots of the real UI.  Every entry refers to
real IFR statements (looked up by their English prompt), so values, options,
help texts, defaults and visibility rules all still come from the BIOS image.

Entry kinds:
  ('items', prompt, ...)   all statements with this prompt from the first form
                           (searched in `search` order) that contains it
  ('form', form_id)        all top-level statements of a form, in order
  ('ref', title, target)   a link to a form id or to a virtual form name
  ('subtitle', text)       orange section title
  ('text', prompt, dyn)    a read-only line whose value is generated at runtime
  ('blank',)               an empty row
  ('rest', [form_ids], exclude_prompts)
                           every top-level statement of these forms that was not
                           used elsewhere on the page
"""

# Form ids in the F9a image
F_SETUP, F_MIT, F_SYSTEM, F_PERIPH, F_CHIPSET = 0x2710, 0x2711, 0x2712, 0x2713, 0x2714
F_BIOS, F_POWER, F_EXIT = 0x2716, 0x2717, 0x2718
F_ADV_FREQ, F_ADV_CPU, F_ADV_MEM, F_ADV_VOLT = 0x2A2D, 0x2A3C, 0x2A79, 0x2A90
F_CPU_VOLT, F_DRAM_VOLT, F_DDR5_VOLT = 0x2A92, 0x2A94, 0x2B3C
F_PC_HEALTH, F_MISC_SETTINGS, F_FAVORITES = 0x2AC8, 0x2B33, 0x2B6E
F_SPD_INFO, F_PLUGIN_DEVICES = 0x28F6, 0x2D8D

TWEAKER_SEARCH = [F_ADV_FREQ, F_ADV_CPU, F_ADV_MEM, F_CPU_VOLT, F_DRAM_VOLT, F_DDR5_VOLT,
                  0x2A93, 0x2A95, 0x2A91, F_MISC_SETTINGS]

VIRTUAL_FORMS = {
    'tweaker': dict(title='Tweaker', search=TWEAKER_SEARCH, entries=[
        ('items', 'GIGABYTE PerfDrive'),
        ('items', 'CPU Upgrade'),
        ('items', 'CPU Base Clock'),
        ('items', 'Enhanced Multi-Core Performance'),
        ('items', 'Performance CPU Clock Ratio'),
        ('items', 'Efficiency CPU Clock Ratio'),
        ('items', 'Max Ring Ratio'),
        ('items', 'Min Ring Ratio'),
        ('items', 'IGP Ratio'),
        ('ref', 'Advanced CPU Settings', F_ADV_CPU),
        ('blank',),
        ('items', 'DDR5 Auto Booster'),
        ('items', 'High Bandwidth'),
        ('items', 'Low Latency'),
        ('items', 'DDR5 XMP Booster'),
        ('items', 'Extreme Memory Profile(X.M.P.)'),
        ('items', 'System Memory Multiplier'),
        ('ref', 'Advanced Memory Settings', F_ADV_MEM),
        ('blank',),
        ('subtitle', 'CPU/PCH Voltage Control'),
        ('items', 'Vcore Voltage Mode'),
        ('items', 'CPU Vcore'),
        ('items', 'Dynamic Vcore(DVID)'),
        ('items', 'BCLK Adaptive Voltage'),
        ('items', 'CPU Graphics Voltage (VAXG)'),
        ('items', 'CPU RING Voltage'),
        ('items', 'CPU RING Voltage Offset'),
        ('items', 'Internal L2Atom Override Mode'),
        ('items', 'Internal L2Atom'),
        ('items', 'Internal L2Atom Offset'),
        ('items', 'Internal VCCSA'),
        ('items', 'CPU VCCIN AUX'),
        ('items', 'VCC1P05'),
        ('items', 'V1P8 CPU'),
        ('items', 'VCC1V8P'),
        ('ref', 'Advanced Voltage Settings', F_ADV_VOLT),
        ('blank',),
        ('subtitle', 'DRAM Voltage Control'),
        ('items', 'VDDQ CPU'),
        ('items', 'VDD2 CPU'),
        ('ref', 'DDR5 Voltage Control', F_DDR5_VOLT),
        ('blank',),
        ('ref', 'SPD Info', F_SPD_INFO),
        ('ref', 'Miscellaneous Settings', F_MISC_SETTINGS),
    ]),

    'settings': dict(title='Settings', entries=[
        ('ref', 'Platform Power', F_POWER),
        ('ref', 'IO Ports', 'io_ports'),
        ('ref', 'Miscellaneous', 'misc'),
        ('blank',),
        ('action', 'Option Search (Hot Key: Alt-F)', 'search'),
        ('blank',),
        ('ref', 'PC Health Status', F_PC_HEALTH),
        ('ref', 'Smart Fan 6', '@smartfan'),
    ]),

    'io_ports': dict(title='IO Ports', search=[F_PERIPH, F_CHIPSET], entries=[
        ('items', 'Initial Display Output'),
        ('items', 'Internal Graphics'),
        ('items', 'DVMT Pre-Allocated'),
        ('items', 'Aperture Size'),
        ('items', 'PCIE Bifurcation Support'),
        ('items', 'OnBoard LAN Controller'),
        ('items', 'Audio Controller'),
        ('items', 'Above 4G Decoding'),
        ('items', 'Above 4GB MMIO BIOS assignment'),
        ('items', 'Re-Size BAR Support'),
        ('items', 'IOAPIC 24-119 Entries'),
        ('items', 'Gigabyte Utilities Downloader Configuration'),
        ('items', 'Super IO Configuration'),
        ('items', 'USB Configuration'),
        ('items', 'Network Stack Configuration'),
        ('items', 'NVMe Configuration'),
        ('items', 'SATA Configuration'),
        ('items', 'VMD setup menu'),
        ('items', 'Thunderbolt(TM) Configuration'),
        ('items', 'OffBoard SATA Controller Configuration'),
        ('items', 'ASMT Configuration'),
    ]),

    'misc': dict(title='Miscellaneous', search=[F_PERIPH, F_CHIPSET], entries=[
        ('rest', [F_PERIPH, F_CHIPSET], [
            # Easy-mode data panels and things shown on the IO Ports page
            'Information', 'CPU Temperature', 'PC Health', 'EZ OC', 'DRAM Status',
            'SATA Information', 'FAN Profile', 'Intel Rapid Storage Tech.',
            'Standard Profile', 'RGB Fusion', 'Plug in Devices Info',
        ]),
    ]),

    'sysinfo': dict(title='System Info.', search=[F_SYSTEM], entries=[
        ('items', 'Model Name'),
        ('items', 'BIOS Version'),
        ('items', 'BIOS Date'),
        ('items', 'BIOS ID'),
        ('blank',),
        ('text', 'Processor Type', 'cpu_type'),
        ('text', 'Processor CPUID', 'cpu_id'),
        ('text', 'Processor Speed', 'cpu_speed'),
        ('text', 'Processor Clock', 'cpu_clock'),
        ('text', 'Installed Memory', 'mem_size'),
        ('blank',),
        ('text', 'LAN MAC Address', 'lan_mac'),
        ('blank',),
        ('items', 'Access Level'),
        ('items', 'System Language'),
        ('blank',),
        ('items', 'System Date'),
        ('items', 'System Time'),
        ('blank',),
        ('ref', 'Plug in Devices Info', F_PLUGIN_DEVICES),
        ('ref', 'Q-Flash', '@qflash'),
    ]),
}

# Top tab bar (Advanced Mode).  "form" is a real form id or a virtual form.
TABS = [
    dict(title='Favorites (F11)', form='favorites', icon='star'),
    dict(title='Tweaker', form='tweaker', icon='gauge'),
    dict(title='Settings', form='settings', icon='gear'),
    dict(title='System Info.', form='sysinfo', icon='info'),
    dict(title='Boot', form=F_BIOS, icon='power'),
    dict(title='Save & Exit', form=F_EXIT, icon='exit'),
]

# Items that carry an orange star in a fresh BIOS (the default favourites).
DEFAULT_FAVORITES = [
    'Enhanced Multi-Core Performance', 'Performance CPU Clock Ratio',
    'Efficiency CPU Clock Ratio', 'Min Ring Ratio', 'IGP Ratio',
    'Extreme Memory Profile(X.M.P.)', 'System Memory Multiplier', 'CPU Vcore',
    'Dynamic Vcore(DVID)',
]

# Statements whose behaviour the emulator implements itself.
ACTIONS = {
    'Save & Exit Setup': 'save_exit',
    'Exit Without Saving': 'exit_nosave',
    'Load Optimized Defaults': 'load_defaults',
    'Save Profiles': 'save_profile',
    'Load Profiles': 'load_profile',
}

# Prompt -> runtime-generated value for read-only text lines.
DYNAMIC_TEXT = {
    'Model Name': 'model', 'BIOS Version': 'bios_version', 'BIOS Date': 'bios_date',
    'BIOS ID': 'bios_id',
    'Case Open': 'hw_case_open', 'CPU Vcore': 'hw_vcore', 'CPU VCCIN AUX': 'hw_vccin_aux',
    'CPU VCCSA': 'hw_vccsa', 'VDD2 CPU': 'hw_vdd2', 'PCH 1.8V': 'hw_pch18',
    '+3.3V': 'hw_3v3', '+5V': 'hw_5v', 'PCH 0.82V': 'hw_pch082', '+12V': 'hw_12v',
    'CPU VAXG': 'hw_vaxg',
}

# Questions the emulator needs to know about (prompt -> key).  The first
# statement with that prompt is used.
SPECIAL = {
    'Preferred Operating Mode': 'preferred_mode',
    'CSM Support': 'csm',
    'Full Screen LOGO Show': 'full_logo',
    'Fast Boot': 'fast_boot',
    'Bootup NumLock State': 'numlock',
    'Security Option': 'security_option',
    'Administrator Password': 'admin_password',
    'User Password': 'user_password',
    'System Language': 'language',
    'Boot Option #%d': 'boot_option',
    'Driver Option #%d': 'driver_option',
    'Extreme Memory Profile(X.M.P.)': 'xmp',
    'System Memory Multiplier': 'mem_multiplier',
    'Performance CPU Clock Ratio': 'p_ratio',
    'Efficiency CPU Clock Ratio': 'e_ratio',
    'CPU Base Clock': 'bclk',
    'CPU Vcore': 'vcore',
    'Memory Boot Mode': 'mem_boot_mode',
    'Memory Channel Detection Message': 'mem_detect_msg',
    'DDR5 XMP Booster': 'xmp_booster',
    'High Bandwidth': 'high_bandwidth',
    'Low Latency': 'low_latency',
    'GIGABYTE PerfDrive': 'perfdrive',
    'Above 4G Decoding': 'above4g',
    'Re-Size BAR Support': 'rebar',
    'Intel (VMX) Virtualization Technology': 'vmx',
    'ErP': 'erp',
}

# Where to look first for SPECIAL prompts that exist on several forms.
SPECIAL_FORM = {
    'Fast Boot': F_BIOS,
}

# Hardware state that the real firmware computes at boot and keeps in
# volatile variables.  The values describe a desktop Raptor Lake-S system on
# a Z790 board with an administrator logged in; they decide which menu items
# are visible, so they were tuned by comparing against real screenshots.
VOLATILE_PROFILE = {
    'SystemAccess': {0: (0, 1)},                 # 0 = administrator
    'SetupVolatileData': {2: (2, 1),             # platform flavour: desktop
                          3: (1, 1)},            # platform type: traditional
    # GIGABYTE "M.I.T. attributes": one byte per tuning feature, set when
    # the installed CPU / DIMMs support it (an unlocked i9 with XMP DDR5).
    'ProcMitAttrib': {'*': (1, 1)},
    'MemMitAttrib': {'*': (1, 1)},
    'HswMitAttrib': {'*': (1, 1)},
    'AdvMitAttrib': {'*': (1, 1)},
    'NBPlatformData': {3: (1, 1),                # integrated graphics present
                       4: (1, 1)},               # ... and enabled
}

# Option texts that the real firmware fills in at runtime.
OPTION_TEXT = {
    ('Intel Default Settings', 3): 'Extreme',
}

# Strings that only exist inside the GIGABYTE GUI binary (not in the HII
# string packages).  English only.
EXTRA_STRINGS = [
    'Processor Type', 'Processor CPUID', 'Processor Speed', 'Processor Clock',
    'Installed Memory', 'LAN MAC Address', 'IO Ports', 'Platform Power', 'Miscellaneous',
    'Smart Fan 6', 'Q-Flash', 'Plug in Devices Info',
]

# UI texts used by the emulator, looked up (in this order) in the setup
# strings, the AMITSE strings and EXTRA_STRINGS, so that translations come
# from the BIOS whenever it has them.  ('prefix', text) matches the first
# string that starts with text.
NAMED_STRINGS = {
    'general_help': 'General Help',
    'general_help_text': ('prefix', '→← : Select Screen'),
    'ok': 'Ok', 'cancel': 'Cancel', 'yes': 'Yes', 'no': 'No',
    'enabled': 'Enabled', 'disabled': 'Disabled', 'auto': 'Auto',
    'save_exit': 'Save & Exit Setup', 'save_exit_q': 'Save configuration and exit?',
    'exit_nosave': 'Exit Without Saving', 'quit_nosave_q': 'Quit without saving?',
    'load_defaults': 'Load Optimized Defaults', 'load_defaults_q': 'Load Optimized Defaults?',
    'load_prev': 'Load Previous Values', 'load_prev_q': 'Load Previous Values?',
    'select_boot': 'Please select boot device:',
    'boot_help': '↑ and ↓ to move selection\r\nENTER to select boot device\r\nESC to boot using defaults',
    'enter_setup': 'Enter Setup',
    'qflash': 'Q-Flash', 'qflash_q': 'Enter Q-Flash Utility?',
    'bios_reset': 'BIOS has been reset - Please decide how to continue',
    'reset_boot': 'Load optimized defaults then boot',
    'reset_reboot': 'Load optimized defaults then reboot',
    'enter_bios': 'Enter BIOS',
    'option_search': 'Option Search',
    'search_hint': ('prefix', 'Key in word(s) to search'),
    'no_match': 'No Match Found',
    'enter_password': 'Enter Password', 'invalid_password': 'Invalid Password',
    'create_password': 'Create New Password', 'confirm_password': 'Confirm New Password',
    'enter_current_password': 'Enter Current Password',
    'clear_password_q': 'Clear Old Password. Continue?',
    'press_del': 'Press <DEL> to enter setup.',
    'entering_setup': 'Entering Setup...',
    'case_open': 'CASE status opened ...',
    'warning': 'WARNING', 'error': 'ERROR', 'invalid_range': 'Invalid Input Range',
    'easy_mode': 'Easy Mode', 'advanced_mode': 'Advanced Mode',
    'help_f1': 'Help (F1)', 'smart_fan_f6': 'Smart Fan 6 (F6)', 'qflash_f8': 'Q-Flash (F8)',
    'load_defaults_f7': 'Load Defaults (F7)', 'save_exit_f10': 'Save & Exit (F10)',
    'search_altf': 'Search (Alt-F)', 'favorites_f11': 'Favorites (F11)',
    'information': 'Information', 'dram_status': 'DRAM Status', 'pc_health': 'PC Health',
    'smart_fan': 'Smart Fan 6', 'boot_sequence': 'Boot Sequence', 'quick_access': 'Quick Access',
    'perfdrive': 'GIGABYTE PerfDrive',
    'cpu': 'CPU', 'memory': 'Memory', 'voltage': 'Voltage', 'frequency': 'Frequency',
    'bclk': 'BCLK', 'temperature': 'Temperature', 'size': 'Size',
    'module_mfg': 'Module MFG ID', 'dram_mfg': 'DRAM MFG ID',
    'save_profiles': 'Save Profiles', 'load_profiles': 'Load Profiles',
    'profile_saved': 'Profile Saved', 'profile_loaded': 'Profile Loaded',
    'profile_not_found': 'Profile Not Found', 'setup_profile': 'Setup Profile',
    'print_screen_saved': 'Screenshot saved',
    'no_boot_device': 'Reboot and Select proper Boot device\r\nor Insert Boot Media in selected Boot device and press a key',
    'not_emulated': 'This function is not available in the emulator.',
    'qflash_title': 'Q-Flash', 'update_bios': 'Update BIOS', 'save_bios': 'Save BIOS',
    'qflash_no_drive': 'No Drive Found',
    'mb': 'MB', 'bios_ver': 'BIOS Ver.', 'ram': 'RAM', 'microcode': 'Microcode',
    'fan_speed': 'Fan Speed', 'system_info': 'System Information', 'exit': 'Exit',
}
