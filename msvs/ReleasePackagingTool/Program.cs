using CommandLine;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;

namespace ReleasePackagingTool
{
	internal class Program
	{
		#region Private Fields

		private readonly string rootPath;
		private const string versionReplacementText = "CurrentRedisVersion";

		public Program(string rootPath)
		{
			this.rootPath = rootPath;
		}

		#endregion Private Fields

		#region Private Methods

		private static void Main(string[] args)
		{
			try
			{
				Parser.Default.ParseArguments<CmdLineOptions>(args).WithParsed(cmdLineOptions =>
				{
					string assemblyDirectory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
					var rootPath = cmdLineOptions.RootPath ?? Path.GetFullPath(Path.Combine(assemblyDirectory, @"..\..\..\..\..\"));

					Program p = new Program(rootPath);

					if (!string.IsNullOrWhiteSpace(cmdLineOptions.PrepareNewVersion))
					{
						p.PrepareNewVersion(cmdLineOptions.PrepareNewVersion);
						p.UpdateNuSpecFiles(cmdLineOptions.PrepareNewVersion);
					}

					if (!cmdLineOptions.SkipPackage)
					{
						var version = p.GetRedisVersion();
						p.UpdateNuSpecFiles(version);
						p.BuildReleasePackage(version, "x64");

						Console.Write("Release packaging complete.");
					}
					else
					{
						Console.WriteLine("Creation of release package skipped.");
					}

					Environment.ExitCode = 0;
				});
			}
			catch (Exception ex)
			{
				Console.WriteLine("Error. Failed to finish release packaging.\n" + ex.ToString());
				Environment.ExitCode = -1;
			}
		}

		private void BuildReleasePackage(string version, string platform)
		{
			string releasePackageDir = GetTargetPath(@"msvs\BuildRelease\Redis-" + version + @"\");
			if (Directory.Exists(releasePackageDir) == false)
			{
				Directory.CreateDirectory(releasePackageDir);
			}

			string releasePackagePath = GetTargetPath(releasePackageDir + "Redis-" + platform + "-" + version + ".zip");
			ForceFileErase(releasePackagePath);

			string executablesRoot = GetTargetPath(@"msvs\" + platform + @"\Release");
			List<Tuple<string /*source*/, string /*target name*/>> executableNames = new List<Tuple<string, string>>()
			{
				Tuple.Create("redis-benchmark.exe", "redis-benchmark.exe"),
				Tuple.Create("redis-server.exe", "redis-check-aof.exe"),
				Tuple.Create("redis-server.exe", "redis-check-rdb.exe"),
				Tuple.Create("redis-cli.exe", "redis-cli.exe"),
				Tuple.Create("redis-server.exe", "redis-server.exe"),
			};
			List<Tuple<string, string>> symbolNames = new List<Tuple<string, string>>()
			{
				Tuple.Create("redis-benchmark.pdb", "redis-benchmark.pdb"),
				Tuple.Create("redis-server.pdb", "redis-check-aof.pdb"),
				Tuple.Create("redis-server.pdb", "redis-check-rdb.pdb"),
				Tuple.Create("redis-cli.pdb","redis-cli.pdb"),
				Tuple.Create("redis-server.pdb", "redis-server.pdb"),
			};
			List<string> dependencyNames = new List<string>()
			{
				"EventLog.dll"
			};
			string documentsRoot = GetTargetPath(@"msvs\setups\documentation");
			List<string> documentNames = new List<string>()
			{
				"README.txt",
				"redis.windows.conf",
				"redis.windows-service.conf"
			};
			List<string> releaseNotes = new List<string>()
			{
				"RELEASENOTES.txt",
				"00-RELEASENOTES"
			};

			using (ZipArchive archive = ZipFile.Open(releasePackagePath, ZipArchiveMode.Create))
			{
				foreach (var executableName in executableNames)
				{
					archive.CreateEntryFromFile(Path.Combine(executablesRoot, executableName.Item1), executableName.Item2);
				}
				foreach (var symbolName in symbolNames)
				{
					archive.CreateEntryFromFile(Path.Combine(executablesRoot, symbolName.Item1), symbolName.Item2);
				}
				foreach (string dependencyName in dependencyNames)
				{
					archive.CreateEntryFromFile(Path.Combine(executablesRoot, dependencyName), dependencyName);
				}
				foreach (string documentName in documentNames)
				{
					archive.CreateEntryFromFile(Path.Combine(documentsRoot, documentName), documentName);
				}
				foreach (string notes in releaseNotes)
				{
					archive.CreateEntryFromFile(Path.Combine(rootPath, notes), notes);
				}
			}
		}

		private void CreateTextFileFromTemplate(string templatePath, string documentPath, string toReplace, string replaceWith)
		{
			string replacedText;
			using (TextReader trTemplate = File.OpenText(templatePath))
			{
				string templateText = trTemplate.ReadToEnd();
				replacedText = templateText.Replace(toReplace, replaceWith);
			}

			ForceFileErase(documentPath);

			using (TextWriter twDoc = File.CreateText(documentPath))
			{
				twDoc.Write(replacedText);
				twDoc.Close();
			}
		}

		private void ForceFileErase(string file)
		{
			if (File.Exists(file))
			{
				File.Delete(file);
			}
		}

		private string GetTargetPath(string filePath)
		{
			if (Path.IsPathRooted(filePath))
			{
				return filePath;
			}

			return Path.Combine(rootPath, filePath);
		}

		private string GetRedisVersion()
		{
			TextReader tr = File.OpenText(GetTargetPath(@"src\version.h"));
			string line = tr.ReadLine();
			int start = line.IndexOf('\"');
			int last = line.LastIndexOf('\"');
			return line.Substring(start + 1, last - start - 1);
		}

		private void UpdateNuSpecFiles(string version)
		{
			string chocTemplate = GetTargetPath(@"msvs\setups\chocolatey\template\redis.nuspec.template");
			string chocDocument = GetTargetPath(@"msvs\setups\chocolatey\redis.nuspec");
			CreateTextFileFromTemplate(chocTemplate, chocDocument, versionReplacementText, version);

			string nugetTemplate = GetTargetPath(@"msvs\setups\nuget\template\redis.nuspec.template");
			string nugetDocument = GetTargetPath(@"msvs\setups\nuget\redis.nuspec");
			CreateTextFileFromTemplate(nugetTemplate, nugetDocument, versionReplacementText, version);
		}

		/// <summary>
		/// Updates some source files that keep track of current Redis for Windows version
		/// with newly provided <paramref name="prepareNewVersion"/>.
		/// </summary>
		/// <param name="prepareNewVersion">New version</param>
		private void PrepareNewVersion(string prepareNewVersion)
		{
			Console.WriteLine("Preparing new version: " + prepareNewVersion);

			//update src/version.h
			File.WriteAllText(GetTargetPath(@"src\version.h"), $"#define REDIS_VERSION \"{prepareNewVersion}\"\r\n", System.Text.Encoding.ASCII);

			//update msvs/RedisForWindows.rc
			var rcFilePath = GetTargetPath(@"msvs\RedisForWindows.rc");
			var rcLines = File.ReadAllLines(rcFilePath);
			var versionWithCommas = prepareNewVersion.Replace('.', ',');

			for (int i = 0, j = rcLines.Length; i < j; i++)
			{
				var line = rcLines[i];

				if (line.IndexOf("FILEVERSION", StringComparison.InvariantCultureIgnoreCase) > -1
					|| line.IndexOf("PRODUCTVERSION", StringComparison.InvariantCultureIgnoreCase) > -1) {
					rcLines[i] = Regex.Replace(line, @"(?<optname>FILEVERSION|PRODUCTVERSION)\s+[1-9][0-9,]+", "${optname} " + versionWithCommas);
					rcLines[i] = Regex.Replace(rcLines[i], @"VALUE ""(?<optname>FileVersion|ProductVersion)"", ""[1-9]+[0-9.]+""", "VALUE \"${optname}\", \"" + prepareNewVersion + "\"");
				}
			}

			File.WriteAllLines(rcFilePath, rcLines, Encoding.ASCII);

			//update Product.wxs
			var wxsPath = GetTargetPath(@"msvs\msi\RedisMsi\Product.wxs");
			var wxsLines = File.ReadAllLines(wxsPath, Encoding.UTF8);

			for (int i = 0, j = wxsLines.Length; i < j; i++)
			{
				var line = wxsLines[i];

				if (line.Contains("Version=\""))
				{
					wxsLines[i] = Regex.Replace(line, "\"[1-9][0-9.]+\"", $"\"{prepareNewVersion}\"");
					break;
				}
			}

			File.WriteAllLines(wxsPath, wxsLines, Encoding.UTF8);
		}

		#endregion Private Methods
	}
}