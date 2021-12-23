# OctoPrint-Desktop
An OctoPrint desktop notifier.

# Building
OctoPrint-Desktop can be built with cmake. 

## Linux
You need a few dependencies first:
 - gtk3
 - libsoup
 - json-glib

This should be sufficient on Debian based systems, although package names may vary slightly
```terminal
sudo apt install build-essential cmake libgtk-3-dev libsoup2.4-dev libjson-glib-dev
```

Then, build like any other cmake project:
 - create a build directory: `mkdir build`
 - enter it: `cd build`
 - configure the cmake project: `cmake -DCMAKE_BUILD_TYPE=Release ..`
 - build: `make -j6`

## Windows
TODO - in theory this should be possible and very similar to Linux, using MSVC and cmake. A full Gtk3 stack + libsoup + libjson-glib would be necessary.

# Setup and Configuration

# Setup
The binary `octoprint-desktop` can be run directly from the build folder once it's compiled, but it may be more convenient to move it to another location, such as `~/bin` or `/usr/bin`.

The `octoprint-tentacle.svg` icon file does need to be accessible. This can be accomplished by copying it to `~/.icons` or `~/.local/share/icons` for the current user, or `/usr/share/icons` for the entire system.

The application can be added to the desktop environment's application menu by editing `scripts/OctoPrint-Desktop.desktop` and copying it to `~/.local/share/applications`. 

*Note: it is possible to run multiple instances of OctoPrint-Desktop for multiple instances of OctoPrint. Create separate configuration files and use the `--config` command line argument. You can also create multiple desktop files and name them appropriately.*

Once the desktop file is created, the DE can be configured to automatically start OctoPrint-Desktop on startup/login.

# Configuration
By default, the program will look for a configuration file named `octoprint-deskop.json` in the user's home directory. This can be overriden with the command line argument `--config`, ie. `--config=/path/to/some.json`.

The configuration file is a json file with the following format, any missing value is given the default below:

```json
{
    "printerName": "3d printer",
    "octoprintURL": "http://octopi.local",
    "apiKey": "invalidapikey",
    "statusText": {
        "notConnected": "{printer-name}\nNot connected to OctoPrint",
        "offline": "{printer-name}\nPrinter offline",
        "offlineError": "{printer-name}\nPrinter offline after error",
        "ready": "{printer-name}\nPrinter ready\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "cancelling": "{printer-name}\nCancelling print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "pausing": "{printer-name}\nPausing print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "paused": "{printer-name}\nPrint Paused at {print-progress}: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "printing": "{printer-name}\nPrinting: {print-filename}\n{print-progress}, {print-timeleft} remaining\nLayer {print-currentLayer} of {print-totalLayers}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
    },
    "eventNotification": [
    ]
}
```

## Status Text
Each status has a template that will be evaluated and shown in both the tooltip and first item of the tray icon menu for the following statuses:
 - `notConnected` - when application isn't connected to the OctoPrint server
 - `offline` - the application is connected to OctoPrint, but the printer itself is offline and not connected
 - `offlineError` - same as `offline` except the printer isn't connected due to an error
 - `cancelling` - a print is currently in the process of being cancelled
 - `pausing` - a print is currently in the process of being paused
 - `paused` - a print is in progress, but paused
 - `printing` - a print is in progress

## Event Notifications
OctoPrint-Desktop will show a desktop notification on the configured events. By default, no events are configured. Events can be specified as follows:
```json
{
    ...
    "eventNotification" : [
        {"event": "Connected", "priority": "low", "template": "Connected to printer"},
    ]
}
```

Each entry has the following values:
 - `event` - the name of the event, see https://docs.octoprint.org/en/master/events/index.html#available-events. It's also possible to use plugin generated events
 - `priority` - one of `low`, `normal`, `high` or `urgent`. Corresponds to https://docs.gtk.org/gio/enum.NotificationPriority.html
 - `template` - a template, similar to the status text templates. Also see template variables below

Some more examples:
```json
{
    ...
    "eventNotification" : [
        {"event": "Connected", "priority": "low", "template": "Connected to printer"},
        {"event": "Disconnected", "priority": "high", "template": "Disconnected from printer"},
        {"event": "Error", "priority": "urgent", "template": "Printer error: {payload-error}"},
        {"event": "Upload", "priority": "normal", "template": "File uploaded to {payload-target} storage:\n{payload-name}"},
        {"event": "PrintStarted", "priority": "low", "template": "Print started:\n{payload-name}"},
        {"event": "PrintDone", "priority": "normal", "template": "Print finished:\n{payload-name}"},
        {"event": "PrintFailed", "priority": "normal", "template": "Print failed ({payload-reason}):\n{payload-name}"},
        {"event": "MovieDone", "priority": "normal", "template": "Timelapse rendering complete:\n{payload-movie_basename}"}
    ]
}
```

## Template Variables
The text shown for status and event notifications can contain variables that will be replaced at run time. Each variable starts and ends with brackets (`{}`) and is named in the form of `category-name` or `category-name-detail`, such as `{printer-name}`.

The following variables are available:
 - `{printer-name}` - the printer name giving in the configuration file
 - `{print-filename}` - the display filename of the current print
 - `{print-progress}` - the progress of the current print
 - `{print-timeleft}` - the time remaining on the current print, in N days N hours N minutes
 - `{print-currentLayer}` - the current layer of the current print, requires Display Layer Progress plugin
 - `{print-totalLayers}` - the total number of layers of the current print, requires Display Layer Progress plugin
 - `{payload-<name>}` - the value of `<name>` from the event payload data (only available on event notifications)
 - `{temp-<name>-target}` - target temperature of the `<name>` heater. `<name>` can be `bed`, `chamber`, or `tool0`, `tool1`, etc.
 - `{temp-<name>-actual}` - same as `{temp-<name>-target}` but the actual temperature
 - `{temp-<name>-offset}` - same as `{temp-<name>-target}` but the offset temperature

# Example Configuration File
```json
{
    "printerName": "Ender 3 Pro",
    "octoprintURL": "http://printerpi.local/ender-3-pro",
    "apiKey": "anapikey",
    "statusText":{
        "offline": "{printer-name}\nPrinter offline",
        "offlineError": "{printer-name}\nPrinter offline after error",
        "ready": "{printer-name}\nPrinter ready\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "cancelling": "{printer-name}\nCancelling print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "pausing": "{printer-name}\nPausing print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "paused": "{printer-name}\nPrint Paused at {print-progress}: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}",
        "printing": "{printer-name}\nPrinting: {print-filename}\n{print-progress}, {print-timeleft} remaining\nLayer {print-currentLayer} of {print-totalLayers}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
    },
    "eventNotification": [
        {"event": "Connected", "priority": "low", "template": "Connected to printer"},
        {"event": "Disconnected", "priority": "high", "template": "Disconnected from printer"},
        {"event": "Error", "priority": "urgent", "template": "Printer error: {payload-error}"},
        {"event": "Upload", "priority": "normal", "template": "File uploaded to {payload-target} storage:\n{payload-name}"},
        {"event": "PrintStarted", "priority": "low", "template": "Print started:\n{payload-name}"},
        {"event": "PrintDone", "priority": "normal", "template": "Print finished:\n{payload-name}"},
        {"event": "PrintFailed", "priority": "normal", "template": "Print failed ({payload-reason}):\n{payload-name}"},
        {"event": "MovieDone", "priority": "normal", "template": "Timelapse rendering complete:\n{payload-movie_basename}"}
    ]    
}
```

# Multiple OctoPrint Servers
Multiple OctoPrint servers can be monitored by supplying multiple server configurations in the config file. In this case, the top level node of the configuration should be an array instead of an object. For example:
```json
[
    {
        "printerName": "Ender 3 Pro",
        "octoprintURL": "http://printerpi.local/ender-3-pro",
        "apiKey": "anapikey"
    },
    {
        "printerName": "Ender 3 v2",
        "octoprintURL": "http://printerpi.local/ender-3-v2",
        "apiKey": "anapikey"
    }
]
```

# OctoPrint Tentacle Icon
The OctoPrint tentacle icon is copyright the OctoPrint Project and licensed under the AGLPv3 License. See https://github.com/OctoPrint/OctoPrint