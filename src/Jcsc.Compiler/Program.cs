using System.CommandLine;
using Jcsc.Compiler.Cli;

namespace Jcsc.Compiler;

/// <summary>
/// CLI entrypoint for the custom C# compiler.
/// </summary>
public static class Program
{
    public static async Task<int> Main(string[] args)
    {
        var inputArg = new Argument<FileInfo>("input", "Path to input .cs file.")
        {
            Arity = ArgumentArity.ExactlyOne
        };

        var outputOpt = new Option<FileInfo?>(["-o", "--output"], "Output assembly path (.exe or .dll).");
        var dllOpt = new Option<bool>("--dll", "Emit a class library instead of console executable.");
        var debugOpt = new Option<bool>("--debug", "Emit debug-optimized assembly.");

        var root = new RootCommand("JCSC - compilador C# modular con front-end propio + backend Roslyn")
        {
            inputArg, outputOpt, dllOpt, debugOpt,
        };

        root.SetHandler((FileInfo input, FileInfo? output, bool dll, bool debug) =>
        {
            var outputPath = output?.FullName ?? Path.Combine(
                input.DirectoryName ?? Environment.CurrentDirectory,
                Path.GetFileNameWithoutExtension(input.Name) + (dll ? ".dll" : ".exe"));

            if (!input.Exists)
            {
                Console.Error.WriteLine($"Input file not found: {input.FullName}");
                return;
            }

            var code = CompilationPipeline.Compile(input.FullName, outputPath, dll, debug);
            Environment.ExitCode = code;
        }, inputArg, outputOpt, dllOpt, debugOpt);

        return await root.InvokeAsync(args);
    }
}
