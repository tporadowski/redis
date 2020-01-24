using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Reflection;

namespace ReleasePackagingTool
{
	internal class Program
	{
		#region Private Fields

		private static string rootPath;
		private static string versionReplacementText = "CurrentRedisVersion";

		#endregion Private Fields

		#region Private Methods

		private static void Main(string[] args)
		{
			try
			{
				Program p = new Program();

				string assemblyDirectory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
				rootPath = Path.GetFullPath(Path.Combine(assemblyDirectory, @"..\..\..\..\..\"));

				string version;
				version = p.GetRedisVersion();
				p.UpdateNuSpecFiles(version);
				p.BuildReleasePackage(version, "x64");

				Console.Write("Release packaging complete.");
				Environment.ExitCode = 0;
			}
			catch (Exception ex)
			{
				Console.WriteLine("Error. Failed to finish release packaging.\n" + ex.ToString());
				Environment.ExitCode = -1;
			}
		}

		private void BuildReleasePackage(string version, string platform)
		{
			string releasePackageDir = Path.Combine(rootPath, @"msvs\BuildRelease\Redis-" + version + @"\");
			if (Directory.Exists(releasePackageDir) == false)
			{
				Directory.CreateDirectory(releasePackageDir);
			}

			string releasePackagePath = Path.Combine(rootPath, releasePackageDir + "Redis-" + platform + "-" + version + ".zip");
			ForceFileErase(releasePackagePath);

			string executablesRoot = Path.Combine(rootPath, @"msvs\" + platform + @"\Release");
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
			string documentsRoot = Path.Combine(rootPath, @"msvs\setups\documentation");
			List<string> documentNames = new List<string>()
			{
				"redis.windows.conf",
				"redis.windows-service.conf"
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

		private string GetRedisVersion()
		{
			TextReader tr = File.OpenText(Path.Combine(rootPath, @"src\version.h"));
			string line = tr.ReadLine();
			int start = line.IndexOf('\"');
			int last = line.LastIndexOf('\"');
			return line.Substring(start + 1, last - start - 1);
		}

		private void UpdateNuSpecFiles(string version)
		{
			string chocTemplate = Path.Combine(rootPath, @"msvs\setups\chocolatey\template\redis.nuspec.template");
			string chocDocument = Path.Combine(rootPath, @"msvs\setups\chocolatey\redis.nuspec");
			CreateTextFileFromTemplate(chocTemplate, chocDocument, versionReplacementText, version);

			string nugetTemplate = Path.Combine(rootPath, @"msvs\setups\nuget\template\redis.nuspec.template");
			string nugetDocument = Path.Combine(rootPath, @"msvs\setups\nuget\redis.nuspec");
			CreateTextFileFromTemplate(nugetTemplate, nugetDocument, versionReplacementText, version);
		}

		#endregion Private Methods
	}
}