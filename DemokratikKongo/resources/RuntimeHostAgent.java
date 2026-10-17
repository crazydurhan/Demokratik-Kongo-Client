import java.io.File;
import java.lang.instrument.Instrumentation;
import java.net.URI;
import java.nio.file.Paths;

import com.sun.tools.attach.VirtualMachine;

public final class RuntimeHostAgent {

    private RuntimeHostAgent() { }

    public static void main(String[] args) {
        try {
            if (args.length < 1) {
                System.out.println("ERR args: need <pid>");
                System.exit(2);
            }

            String jarPath = Paths.get(RuntimeHostAgent.class.getProtectionDomain()
                    .getCodeSource().getLocation().toURI()).toAbsolutePath().toString();

            VirtualMachine vm = VirtualMachine.attach(args[0]);
            try {
                String options = "";
                if (args.length >= 2)
                    options = "-Dll=" + args[1];
                vm.loadAgent(jarPath, options);
            } finally {
                vm.detach();
            }

            System.out.println("OK");
        } catch (Throwable t) {
            Throwable cause = t.getCause() != null ? t.getCause() : t;
            System.out.println("ERR " + cause.getClass().getSimpleName() + ": " + cause.getMessage());
        }
    }

    public static void agentmain(String options, Instrumentation instrumentation) {
        try {
            String dllPath = null;

            if (options != null && options.startsWith("-Dll=")) {
                dllPath = options.substring(5).trim();
                if (dllPath.length() >= 2 && dllPath.charAt(0) == '"' && dllPath.endsWith("\""))
                    dllPath = dllPath.substring(1, dllPath.length() - 1);
            }

            if (dllPath == null || dllPath.isEmpty()) {
                URI selfUri = RuntimeHostAgent.class.getProtectionDomain().getCodeSource().getLocation().toURI();
                File dir = Paths.get(selfUri).toAbsolutePath().getParent().toFile();
                dllPath = new File(dir, "RuntimeHostCore.dll").getAbsolutePath();
            }

            System.load(new File(dllPath).getAbsolutePath());
            System.out.println("RH_AGENT ok");
        } catch (Throwable t) {
            System.out.println("RH_AGENT err " + t);
        }
    }

    public static void agentmain(String options) {
        agentmain(options, null);
    }
}
