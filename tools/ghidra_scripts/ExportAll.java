// ExportAll.java - headless Ghidra post-script: decompiles every function of the current program
// into one text file (argument 1), each preceded by "// ==== <address> <name>" and the strings it
// references. Output is a READING AID for the specification work and must stay OUTSIDE the
// repository (C:\temp\amiga_eu2\ghidra\) - decompiled code never goes into the port.
//@category EU2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;

public class ExportAll extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		String out = args.length > 0 ? args[0] : "eu2_all.c";
		DecompInterface d = new DecompInterface();
		d.openProgram(currentProgram);
		PrintWriter w = new PrintWriter(new BufferedWriter(new OutputStreamWriter(new FileOutputStream(out), "UTF-8")));
		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		int n = 0, fail = 0;
		ReferenceManager rm = currentProgram.getReferenceManager();
		Listing listing = currentProgram.getListing();
		while (it.hasNext() && !monitor.isCancelled()) {
			Function f = it.next();
			w.println("// ==== " + f.getEntryPoint() + " " + f.getName());
			// strings referenced from the body
			Set<String> strs = new TreeSet<>();
			for (Address a : f.getBody().getAddresses(true)) {
				for (Reference r : rm.getReferencesFrom(a)) {
					Data dt = listing.getDataAt(r.getToAddress());
					if (dt != null && dt.hasStringValue()) {
						String s = String.valueOf(dt.getValue());
						if (s.length() > 120) s = s.substring(0, 120);
						strs.add(s.replace("\n", "\\n"));
					}
				}
			}
			for (String s : strs) w.println("//   str: \"" + s + "\"");
			DecompileResults r = d.decompileFunction(f, 60, monitor);
			if (r != null && r.decompileCompleted()) w.println(r.getDecompiledFunction().getC());
			else { w.println("// (decompile failed)"); fail++; }
			n++;
		}
		w.close();
		println("ExportAll: " + n + " functions, " + fail + " failed -> " + out);
	}
}
