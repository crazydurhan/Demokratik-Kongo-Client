import java.io.File;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;

import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.TickEvent;

/**
 * RuntimeHost loader mod (Forge 1.8.9).
 *
 * Does nothing except hand control to the native core DLL: the launcher
 * downloads/verifies the core and writes the absolute path into
 * %APPDATA%\RuntimeHost\load\current.txt; this mod reads that pointer a
 * couple of seconds after the game starts ticking and System.load()s it.
 * No injection, no cross-process work - the game loads its own library.
 */
@Mod(modid = "runtimehost", name = "RuntimeHost", version = "1.0",
     acceptableRemoteVersions = "*", acceptedMinecraftVersions = "[1.8.9]")
public final class RuntimeHostMod {

    private static final int DELAY_TICKS = 40;

    private int ticks = 0;
    private boolean fired = false;

    public RuntimeHostMod() {
        MinecraftForge.EVENT_BUS.register(this);
        log("loader constructed");
    }

    @SubscribeEvent
    public void onClientTick(TickEvent.ClientTickEvent event) {
        if (fired || event.phase != TickEvent.Phase.START) {
            return;
        }
        if (++ticks < DELAY_TICKS) {
            return;
        }
        fired = true;
        try {
            MinecraftForge.EVENT_BUS.unregister(this);
        } catch (Throwable ignored) {
        }
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                loadCore();
            }
        }, "runtimehost-loader");
        t.setDaemon(true);
        t.start();
    }

    private static void loadCore() {
        String path = readPointer();
        if (path == null || path.isEmpty()) {
            log("no core pointer (current.txt missing/empty)");
            return;
        }
        File dll = new File(path);
        for (int attempt = 1; attempt <= 3; ++attempt) {
            try {
                System.load(dll.getAbsolutePath());
                log("core loaded: " + dll.getAbsolutePath());
                return;
            } catch (Throwable t) {
                log("load attempt " + attempt + " failed: " + t);
                try {
                    Thread.sleep(1000L);
                } catch (InterruptedException ignored) {
                    return;
                }
            }
        }
    }

    private static String readPointer() {
        try {
            String appdata = System.getenv("APPDATA");
            if (appdata == null) {
                return null;
            }
            File p = new File(appdata, "RuntimeHost" + File.separator + "load" + File.separator + "current.txt");
            if (!p.isFile()) {
                return null;
            }
            byte[] raw = java.nio.file.Files.readAllBytes(p.toPath());
            return new String(raw, "UTF-8").trim();
        } catch (Throwable t) {
            log("pointer read failed: " + t);
            return null;
        }
    }

    private static void log(String message) {
        try {
            String appdata = System.getenv("APPDATA");
            File f = new File(appdata, "RuntimeHost" + File.separator + "mod-loader.log");
            File parent = f.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            String line = "[" + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS").format(new Date())
                    + "] " + message + System.lineSeparator();
            FileOutputStream out = new FileOutputStream(f, true);
            try {
                out.write(line.getBytes("UTF-8"));
            } finally {
                out.close();
            }
        } catch (Throwable ignored) {
        }
    }
}
