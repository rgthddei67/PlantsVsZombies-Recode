"""在隔离 Git 仓库中验证头文件提交门禁，不触碰项目暂存区。"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HOOKS = Path(__file__).resolve().parents[1] / ".githooks"


class HeaderHookTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pvz-header-hook-")
        self.repo = Path(self.temp.name)
        self.assertEqual(self.repo.resolve().parent, Path(tempfile.gettempdir()).resolve())
        self.assertTrue(self.repo.name.startswith("pvz-header-hook-"))
        self.addCleanup(self.temp.cleanup)
        # 隔离用户签名、全局 hooks 与 Git 调用方传入的索引/仓库路径。
        self.env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        self.env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        self.git("init", "-q")
        for key, value in (
            ("user.name", "Hook test"),
            ("user.email", "hook-test@example.invalid"),
            ("commit.gpgsign", "false"),
            ("core.autocrlf", "false"),
            ("core.hooksPath", str(self.repo / "empty-hooks")),
        ):
            self.git("config", key, value)
        # 在安装被测 hook 前建立真实历史文件，覆盖兼容旧格式的承诺。
        self.write("legacy.h", b"\xef\xbb\xbf#ifndef LEGACY_H\n#define LEGACY_H\n#endif\n")
        self.git("add", "--", "legacy.h")
        self.git("commit", "-qm", "Legacy fixture")
        self.git("config", "core.hooksPath", str(HOOKS))

    def git(self, *args, check=True):
        result = subprocess.run(
            ["git", *args], cwd=self.repo, env=self.env,
            capture_output=True, encoding="utf-8", errors="replace",
        )
        if check:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def write(self, name, content):
        (self.repo / name).write_bytes(content)

    def commit_header(self, content, name="new.h", expected=True):
        self.write(name, content)
        self.git("add", "--", name)
        self.check_commit(expected)

    def check_commit(self, expected):
        before = self.git("rev-parse", "HEAD").stdout
        result = self.git("commit", "-qm", "Test change", check=False)
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)
        after = self.git("rev-parse", "HEAD").stdout
        self.assertEqual(before != after, expected)

    def test_accepts_lf_crlf_and_all_header_extensions(self):
        for extension, ending in (("h", b"\n"), ("hpp", b"\r\n"), ("hh", b"\n"), ("hxx", b"\n")):
            with self.subTest(extension=extension):
                self.commit_header(b"#pragma once" + ending + b"struct Example {};" + ending,
                                   name="new." + extension)

    def test_accepts_inline_comment(self):
        self.commit_header(b"#pragma once // header guard\n")

    def test_rejects_new_traditional_guard(self):
        self.commit_header(b"#ifndef NEW_H\n#define NEW_H\n#endif\n", expected=False)

    def test_rejects_comment_or_late_directive(self):
        for content in (b"// #pragma once\n", b"/* #pragma once */\n", b"// license\n#pragma once\n",
                        b"\n#pragma once\n", b"#pragma once_more\n", b""):
            with self.subTest(content=content):
                self.commit_header(content, expected=False)

    def test_rejects_new_bom(self):
        self.commit_header(b"\xef\xbb\xbf#pragma once\n", expected=False)

    def test_preserves_modified_legacy_guard(self):
        self.commit_header(b"\xef\xbb\xbf#ifndef LEGACY_H\n#define LEGACY_H\nstruct Legacy {};\n#endif\n",
                           name="legacy.h")

    def test_rejects_removed_legacy_guard(self):
        self.commit_header(b"struct Legacy {};\n", name="legacy.h", expected=False)

    def test_checks_valid_index_despite_invalid_worktree(self):
        self.write("new.h", b"#pragma once\n")
        self.git("add", "--", "new.h")
        self.write("new.h", b"struct Unguarded {};\n")
        self.check_commit(True)
        self.assertEqual(self.git("show", ":new.h").stdout, "#pragma once\n")

    def test_rejects_invalid_index_despite_fixed_worktree(self):
        self.write("new.h", b"struct Unguarded {};\n")
        self.git("add", "--", "new.h")
        self.write("new.h", b"#pragma once\n")
        self.check_commit(False)

    def test_handles_unicode_spaces_and_shell_characters(self):
        self.commit_header(b"#pragma once\n", name="中文 header $(literal).h")

    def test_reads_large_header_without_broken_pipe(self):
        self.commit_header(b"#pragma once\n" + b"// large header body\n" * 10000)

    def test_renamed_path_uses_new_header_rule(self):
        self.git("mv", "--", "legacy.h", "renamed.h")
        self.check_commit(False)
        self.commit_header(b"#pragma once\nstruct Legacy {};\n", name="renamed.h")

    def test_deleted_header_is_ignored(self):
        self.git("rm", "--", "legacy.h")
        self.check_commit(True)

    def test_non_header_change_is_ignored(self):
        self.commit_header(b"documentation\n", name="notes.md")

    def test_mixed_batch_cannot_hide_bad_header(self):
        self.write("bad header.h", b"struct Unguarded {};\n")
        self.write("good.h", b"#pragma once\n")
        self.git("add", "--", "bad header.h", "good.h")
        self.check_commit(False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
