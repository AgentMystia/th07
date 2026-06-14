//@category TH07
// 报告当前程序的分析统计：函数数、节区、入口、符号数。用于 headless 验证分析质量。
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SymbolIterator;

public class ReportStats extends GhidraScript
{
    @Override public void run() throws Exception
    {
        int count = 0, namedCount = 0;
        FunctionIterator it = currentProgram.getListing().getFunctions(true);
        Function firstNamed = null;
        while (it.hasNext())
        {
            Function f = it.next();
            count++;
            String nm = f.getName(true);
            if (!nm.startsWith("FUN_") && !nm.startsWith("th07_") && !nm.startsWith("_") && !nm.equals(f.getName()))
            {
                namedCount++;
                if (firstNamed == null) firstNamed = f;
            }
        }
        println("FUNCTION_COUNT=" + count);
        println("NAMED_COUNT=" + namedCount);

        StringBuilder blocks = new StringBuilder("BLOCKS:");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks())
        {
            blocks.append(" ").append(b.getName()).append("(")
                  .append(b.getStart()).append("-").append(b.getEnd())
                  .append(",size=").append(b.getSize()).append(")");
        }
        println(blocks.toString());
        println("IMAGE_BASE=" + currentProgram.getImageBase());
        println("LANG=" + currentProgram.getLanguageID());
        println("COMPILER=" + currentProgram.getCompilerSpec().getCompilerSpecID());

        int symCount = 0;
        SymbolIterator sit = currentProgram.getSymbolTable().getAllSymbols(true);
        while (sit.hasNext()) { sit.next(); symCount++; }
        println("SYMBOL_COUNT=" + symCount);

        // 前 5 个函数地址+名字
        FunctionIterator it2 = currentProgram.getListing().getFunctions(true);
        int shown = 0;
        StringBuilder fns = new StringBuilder("FIRST_FUNCS:");
        while (it2.hasNext() && shown < 8)
        {
            Function f = it2.next();
            fns.append(" ").append(f.getEntryPoint()).append("=").append(f.getName(true));
            shown++;
        }
        println(fns.toString());
    }
}
