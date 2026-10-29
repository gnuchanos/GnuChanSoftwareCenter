# ~/.config/GnuChanSoftwareCenter/GnuChanSoftwareCenter.py
#
# GnuchanOS software center settings. Written in the same script style as
# every other GnuChan program.

Version = "0.2.0"

gcl_SoftwareCenter.Settings(
    # Whether to show installed programs by default.
    ShowInstalled=True,

    # Whether to check for updates on startup.
    CheckUpdates=True,

    # The package manager to use (only "apt" is supported for now).
    PackageManager="apt",

    # Whether to require authentication for installs.
    RequireAuth=True,
)
