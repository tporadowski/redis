using CommandLine;

namespace ReleasePackagingTool
{
	class CmdLineOptions
	{
		[Option('n', "new-version", HelpText = "Prepares new version")]
		public string PrepareNewVersion { get; set; }

		[Option('p', "root-path", HelpText = "Root path to project folder")]
		public string RootPath { get; set; }

		[Option('s', "skip-package", HelpText = "When true skips preparing the package")]
		public bool SkipPackage { get; set; }
	}
}
