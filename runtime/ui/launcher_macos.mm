#import <AppKit/AppKit.h>

#include "../config.h"
#include "../install/releases.h"

#include <algorithm>
#include <filesystem>
#include <string>

bool Launcher_Run(std::string& dataDir, const std::string&, const std::filesystem::path&)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];

        for (;;)
        {
            const bool dataReady = !dataDir.empty() && Release_Find(Release_InstalledId(dataDir));
            const bool spanish = Config_SpanishInterface();
            NSAlert* launcher = [[NSAlert alloc] init];
            launcher.messageText = @"CastawayRecomp";
            launcher.informativeText = dataReady
                ? (spanish ? @"The Sims 2: Náufragos\nLos datos del juego están listos."
                           : @"The Sims 2: Castaway\nYour extracted Wii disc is ready.")
                : (spanish ? @"Selecciona la carpeta DATA de tu disco de Wii extraído."
                           : @"Choose the DATA folder from your extracted Wii disc.");
            NSButton* play = [launcher addButtonWithTitle:(spanish ? @"Jugar" : @"Play")];
            play.enabled = dataReady;
            [launcher addButtonWithTitle:(spanish ? @"Salir" : @"Quit")];
            [launcher addButtonWithTitle:(spanish ? @"Elegir datos…" : @"Choose Data…")];

            NSButton* fullscreen = [NSButton checkboxWithTitle:(spanish ? @"Abrir en pantalla completa" : @"Open in full screen")
                                                        target:nil action:nil];
            fullscreen.state = g_config.fullscreen ? NSControlStateValueOn : NSControlStateValueOff;
            NSPopUpButton* language = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [language addItemsWithTitles:@[@"日本語", @"English", @"Deutsch", @"Français", @"Español", @"Italiano", @"Nederlands"]];
            [language selectItemAtIndex:std::clamp(g_config.language, 0, 6)];

            NSPopUpButton* interfaceLanguage = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [interfaceLanguage addItemsWithTitles:@[@"English", @"Español latino"]];
            [interfaceLanguage selectItemAtIndex:(spanish ? 1 : 0)];
            NSMutableArray<NSView*>* rows = [NSMutableArray arrayWithArray:@[
                [NSTextField labelWithString:(spanish ? @"Idioma de la interfaz" : @"Interface language")], interfaceLanguage,
                [NSTextField labelWithString:(spanish ? @"Idioma del juego" : @"Game language")], language]];
#ifdef CASTAWAY_DXMT
            NSPopUpButton* scale = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [scale addItemsWithTitles:@[@"1× · 640 × 528", @"2× · 1280 × 1056", @"3× · 1920 × 1584",
                                        @"4× · 2560 × 2112", @"5× · 3200 × 2640", @"6× · 3840 × 3168"]];
            [scale selectItemAtIndex:std::clamp(g_config.internalScale, 1, 6) - 1];
            [rows addObjectsFromArray:@[
                [NSTextField labelWithString:(spanish ? @"Resolución interna" : @"Internal resolution")], scale]];
#endif
            [rows addObject:fullscreen];
            NSStackView* options = [NSStackView stackViewWithViews:rows];
            options.orientation = NSUserInterfaceLayoutOrientationVertical;
            options.alignment = NSLayoutAttributeLeading;
            options.spacing = 8;
            options.frame = NSMakeRect(0, 0, 265, rows.count * 27);
            launcher.accessoryView = options;
            switch ([launcher runModal])
            {
                case NSAlertFirstButtonReturn:
                    if (!dataReady) break;
                    g_config.fullscreen = fullscreen.state == NSControlStateValueOn;
                    g_config.language = static_cast<int>(language.indexOfSelectedItem);
                    g_config.interfaceLanguage = interfaceLanguage.indexOfSelectedItem == 1 ? 4 : 1;
#ifdef CASTAWAY_DXMT
                    g_config.internalScale = static_cast<int>(scale.indexOfSelectedItem) + 1;
#endif
                    Config_Save();
                    return true;
                case NSAlertThirdButtonReturn:
                {
                    NSOpenPanel* picker = [NSOpenPanel openPanel];
                    picker.canChooseDirectories = YES;
                    picker.canChooseFiles = NO;
                    picker.prompt = spanish ? @"Elegir carpeta de datos" : @"Select Data Folder";
                    if ([picker runModal] != NSModalResponseOK) break;
                    const std::string path = picker.URL.fileSystemRepresentation;
                    if (!Release_Find(Release_InstalledId(path)))
                    {
                        NSAlert* error = [[NSAlert alloc] init];
                        error.messageText = spanish ? @"No se reconocieron los datos" : @"Game data could not be recognized";
                        error.informativeText = spanish
                            ? @"Selecciona la carpeta DATA que contiene las carpetas sys y files."
                            : @"Select the extracted DATA folder containing the sys and files directories.";
                        [error runModal];
                        break;
                    }
                    dataDir = path;
                    g_config.dataDir = path;
                    Config_Save();
                    break;
                }
                default:
                    return false;
            }
        }
    }
}
