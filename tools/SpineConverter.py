import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def format_command(command: list[str]) -> str:
	return subprocess.list2cmdline(command)


def locate_executable(name: str) -> Path:
	script_dir = Path(__file__).resolve().parent
	direct_candidate = script_dir / name
	if direct_candidate.exists():
		return direct_candidate

	for candidate in script_dir.rglob(name):
		if candidate.is_file():
			return candidate

	raise FileNotFoundError(
		f"Could not locate {name} under {script_dir}. Ensure the executable is placed alongside this script."
	)


def determine_output_suffix(source_suffix: str, format_option: str) -> str:
	source_suffix = source_suffix.lower()
	if format_option == "same":
		return source_suffix
	if format_option == "json":
		return ".json"
	if format_option == "skel":
		return ".skel"
	if format_option == "other":
		if source_suffix == ".json":
			return ".skel"
		if source_suffix == ".skel":
			return ".json"
		raise ValueError("--format other only applies to .json or .skel files")
	raise ValueError(f"Unsupported format option: {format_option}")


def build_converter_command(executable: Path, input_path: Path, output_path: Path, version: str | None, remove_curve: bool) -> list[str]:
	command = [str(executable), str(input_path), str(output_path)]
	if version:
		command.extend(["-v", version])
	if remove_curve:
		command.append("--remove-curve")
	return command


def build_atlas_command(executable: Path, input_path: Path, output_dir: Path) -> list[str]:
	return [str(executable), str(input_path), str(output_dir)]


def is_spine_43_target(version: str | None) -> bool:
	if not version:
		return False
	parts = version.split(".")
	if len(parts) != 3:
		return False
	try:
		return (int(parts[0]), int(parts[1])) >= (4, 3)
	except ValueError:
		return False


def read_atlas_page_names(atlas_path: Path) -> list[str]:
	"""Return atlas page texture paths in declaration order."""
	text = atlas_path.read_text(encoding="utf-8-sig")
	pages: list[str] = []
	expect_page = True

	for raw_line in text.splitlines():
		line = raw_line.strip()
		if not line:
			expect_page = True
			continue
		if expect_page:
			if ":" in line:
				# Atlas-level metadata can appear before the first page.
				continue
			pages.append(line)
			expect_page = False

	return pages


def normalize_atlas_for_spine43(text: str, expected_first_page: str) -> str:
	# Ark-Models contains atlas files with a physical blank line before the
	# first page. Keep the atlas data otherwise unchanged, but make the first
	# physical line the texture filename for strict/runtime parsers.
	normalized = text.lstrip("\ufeff\r\n\t ")
	if not normalized:
		raise ValueError("Atlas became empty after removing leading whitespace")

	first_line = normalized.splitlines()[0].strip()
	if first_line != expected_first_page:
		raise ValueError(
			f"Atlas first physical line must be texture page '{expected_first_page}', got '{first_line}'"
		)
	return normalized


def atlas_texture_pairs(input_path: Path, output_path: Path) -> tuple[list[str], list[tuple[Path, Path]]]:
	pages = read_atlas_page_names(input_path)
	if not pages:
		raise ValueError(f"Atlas contains no texture page: {input_path}")

	pairs: list[tuple[Path, Path]] = []
	for page_name in pages:
		page_rel = Path(page_name.replace("\\", "/"))
		if page_rel.is_absolute() or ".." in page_rel.parts:
			raise ValueError(f"Unsafe atlas texture path '{page_name}' in {input_path}")

		texture_source = input_path.parent / page_rel
		if not texture_source.is_file():
			raise FileNotFoundError(
				f"Atlas texture does not exist: {texture_source} (referenced by {input_path.name})"
			)
		pairs.append((texture_source, output_path.parent / page_rel))

	return pages, pairs


def copy_atlas_textures_for_43(input_path: Path, output_path: Path) -> list[str]:
	pages, pairs = atlas_texture_pairs(input_path, output_path)
	for texture_source, texture_destination in pairs:
		texture_destination.parent.mkdir(parents=True, exist_ok=True)
		if texture_source.resolve() != texture_destination.resolve():
			shutil.copy2(texture_source, texture_destination)
		print(f"[texture-ready] {texture_destination}")
	return pages


def write_atlas_for_43(input_path: Path, output_path: Path, pages: list[str]) -> None:
	text = input_path.read_text(encoding="utf-8-sig")
	normalized = normalize_atlas_for_spine43(text, pages[0])
	output_path.parent.mkdir(parents=True, exist_ok=True)

	# Write to a non-atlas temporary filename, then atomically replace the
	# target. This prevents Godot from importing a partially written atlas.
	temp_path = output_path.with_name(output_path.name + ".tmp")
	temp_path.write_text(normalized, encoding="utf-8", newline="\n")
	raw = temp_path.read_bytes()
	expected = pages[0].encode("utf-8")
	if raw.startswith(b"\xef\xbb\xbf"):
		temp_path.unlink(missing_ok=True)
		raise ValueError(f"Output atlas still contains a UTF-8 BOM: {output_path}")
	if not raw.startswith(expected + b"\n") and raw != expected:
		preview = raw[:80].decode("utf-8", errors="replace").replace("\r", "\\r").replace("\n", "\\n")
		temp_path.unlink(missing_ok=True)
		raise ValueError(
			f"Output atlas does not start with '{pages[0]}'. First bytes: {preview!r}"
		)
	temp_path.replace(output_path)
	print(f"[atlas-ready] {output_path} (first page: {pages[0]})")


def process_files(args: argparse.Namespace) -> None:
	input_dir = Path(args.input_directory).resolve()
	output_dir = Path(args.output_directory).resolve()

	if not input_dir.exists() or not input_dir.is_dir():
		raise FileNotFoundError(f"Input directory does not exist: {input_dir}")

	output_dir.mkdir(parents=True, exist_ok=True)

	converter_exe = locate_executable("SpineSkeletonDataConverter.exe")
	is_43 = is_spine_43_target(args.version)
	atlas_exe = None if is_43 else locate_executable("SpineAtlasDowngrade.exe")

	files = [
		path for path in input_dir.rglob("*")
		if path.is_file() and path.suffix.lower() in {".json", ".skel", ".atlas"}
	]

	# For Spine 4.3, make every texture physically present before exposing any
	# atlas file to a live Godot editor. Godot's atlas importer may run as soon
	# as the atlas appears and ResourceLoader cannot load a PNG that has not
	# finished/started its own import yet (spine-runtimes issue #2385).
	atlas_pages: dict[Path, list[str]] = {}
	if is_43:
		for path in files:
			if path.suffix.lower() != ".atlas":
				continue
			relative_path = path.relative_to(input_dir)
			output_path = output_dir / relative_path
			atlas_pages[path] = copy_atlas_textures_for_43(path, output_path)

		for path, pages in atlas_pages.items():
			relative_path = path.relative_to(input_dir)
			write_atlas_for_43(path, output_dir / relative_path, pages)

	for path in files:
		suffix = path.suffix.lower()
		relative_path = path.relative_to(input_dir)
		destination_parent = (output_dir / relative_path).parent
		destination_parent.mkdir(parents=True, exist_ok=True)

		if suffix in {".json", ".skel"}:
			output_suffix = determine_output_suffix(suffix, args.format)
			destination_path = output_dir / relative_path.with_suffix(output_suffix)
			command = build_converter_command(
				converter_exe,
				path,
				destination_path,
				args.version,
				args.remove_curve,
			)
			print(f"[converter] {format_command(command)}")
			subprocess.run(command, check=True)
		elif not is_43:
			assert atlas_exe is not None
			command = build_atlas_command(atlas_exe, path, output_dir / relative_path.parent)
			print(f"[atlas] {format_command(command)}")
			subprocess.run(command, check=True)


def parse_arguments(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Batch convert Spine assets using project tools")
	parser.add_argument("input_directory", help="Root directory containing source files")
	parser.add_argument("output_directory", help="Destination directory for converted files")
	parser.add_argument(
		"-v",
		"--version",
		help="Target Spine version (x.y.z). Defaults to source version.",
	)
	parser.add_argument(
		"--format",
		choices=["same", "json", "skel", "other"],
		default="same",
		help="Output format selection rule for .json/.skel files",
	)
	parser.add_argument(
		"--remove-curve",
		action="store_true",
		help="Strip animation curves instead of converting when crossing 3.x/4.x",
	)
	return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
	if argv is None:
		argv = sys.argv[1:]

	try:
		args = parse_arguments(argv)
		process_files(args)
	except FileNotFoundError as exc:
		print(f"Error: {exc}", file=sys.stderr)
		return 1
	except subprocess.CalledProcessError as exc:
		command_repr = format_command(list(exc.cmd)) if isinstance(exc.cmd, (list, tuple)) else str(exc.cmd)
		print(f"Subprocess failed with exit code {exc.returncode}: {command_repr}", file=sys.stderr)
		return exc.returncode or 1
	except ValueError as exc:
		print(f"Error: {exc}", file=sys.stderr)
		return 1

	return 0


if __name__ == "__main__":
	raise SystemExit(main())
