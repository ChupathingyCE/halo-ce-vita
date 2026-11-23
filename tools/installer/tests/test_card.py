# SPDX-License-Identifier: GPL-3.0-only
"""Issue #54: the Vita's files written to its memory card over USB
(VitaShell's USB mode shows it as a drive) or to any folder, instead of FTP:
the card layout (data/haloce-vita/..., the VPK at the root, the README), a
card's root told from a folder, the replace/keep rules, the free-space check,
the folders refused, the command line, and the wizard's Destination page and
run. Every game file here is made up (fakes)."""

import os
import sys
import zipfile

import pytest

import halo_ce_vita_installer as hcv
from test_flow import (advance, buttons, complaints, fake_steam, texts, to_game_files, wait_for_finish,  # noqa: F401
                       wizard)
from test_window import all_widgets, pump, root, stage  # noqa: F401 (root: the fixture)


def make_vpk(path):
    with zipfile.ZipFile(str(path), "w") as archive:
        archive.writestr("eboot.bin", b"x")
        archive.writestr("sce_sys/param.sfo", b"x")
    return path


def card_root(folder):
    """A folder that looks like ux0: in VitaShell's USB mode."""
    for name in ("app", "data", "license", "user"):
        (folder / name).mkdir(parents=True, exist_ok=True)
    (folder / "id.dat").write_bytes(b"x")
    return folder


def staged_out(tmp_path):
    out = tmp_path / "out"
    stage(str(out), xbox=True, pc=True, movies=1)
    return out


def test_a_card_root_is_told_from_a_folder(tmp_path):
    assert hcv.looks_like_card_root(str(card_root(tmp_path / "card")))
    plain = tmp_path / "plain"
    (plain / "data").mkdir(parents=True)
    assert not hcv.looks_like_card_root(str(plain)), "a data folder alone is not a card"
    assert not hcv.looks_like_card_root(str(tmp_path / "missing"))
    # (the card's data, or its data/haloce-vita, chosen: the card's root)
    card = tmp_path / "card"
    (card / "data" / "haloce-vita").mkdir(parents=True)
    assert hcv.card_root_of(str(card / "data")) == str(card)
    assert hcv.card_root_of(str(card / "data" / "haloce-vita")) == str(card)
    assert hcv.card_root_of(str(plain / "data")) == str(plain / "data"), "a plain folder's data is just a folder"
    assert hcv.card_root_of(str(plain / "data" / "haloce-vita")) == str(plain)


def test_the_card_layout_is_the_ftp_one(tmp_path):
    out = staged_out(tmp_path)
    card = card_root(tmp_path / "card")
    vpk = make_vpk(tmp_path / "halo-ce-vita-1.1.3-vita.vpk")
    sent = hcv.staged_uploads(str(out))
    result = hcv.copy_to_card(str(out), str(card), str(vpk), log=lambda line: None)
    assert result["card"] and result["vpk"] == vpk.name and result["copied"] == len(sent) + 1
    game = card / "data" / "haloce-vita"
    assert result["game_folder"] == str(game)
    for item in sent:
        # (each where FTP would put it: ux0:data/haloce-vita[/...] -> <card>/data/haloce-vita[/...])
        relative = item.folder[len(hcv.VITA_GAME_FOLDER):].strip("/")
        path = game / relative / item.name if relative else game / item.name
        assert path.is_file() and path.stat().st_size == item.size, item.name
    assert (game / "default.xbe").is_file() and (game / "maps" / "bitmaps.map").is_file()
    assert (game / "movies" / "movie0.mp4").is_file()
    assert (card / vpk.name).read_bytes() == vpk.read_bytes()
    readme = (card / hcv.CARD_README).read_text(encoding="utf-8")
    assert "straight onto your Vita's memory card" in readme and "press Cross on" in readme and vpk.name in readme
    assert "uma0:" in readme and b"\r\n" in (card / hcv.CARD_README).read_bytes()
    assert not list(card.rglob("*.part"))
    # (nothing else of the card touched)
    assert sorted(os.listdir(str(card))) == sorted(["app", "data", "license", "user", "id.dat", vpk.name,
                                                    hcv.CARD_README])


def test_a_plain_folder_gets_the_same_layout_and_says_what_to_copy(tmp_path):
    out = staged_out(tmp_path)
    folder = tmp_path / "for the vita"
    result = hcv.copy_to_card(str(out), str(folder), log=lambda line: None)
    assert not result["card"] and result["vpk"] == ""
    assert (folder / "data" / "haloce-vita" / "maps" / "a10.map").is_file()
    readme = (folder / hcv.CARD_README).read_text(encoding="utf-8")
    assert "Copy the data folder onto the card's root" in readme and "Install the VPK" in readme


def test_files_there_are_kept_unless_replace_and_the_vpk_is_always_written(tmp_path):
    out = staged_out(tmp_path)
    card = card_root(tmp_path / "card")
    vpk = make_vpk(tmp_path / "halo.vpk")
    hcv.copy_to_card(str(out), str(card), str(vpk), log=lambda line: None)
    again = hcv.copy_to_card(str(out), str(card), str(vpk), log=lambda line: None)
    assert again["copied"] == 1 and again["skipped"] == len(hcv.staged_uploads(str(out))), "only the VPK again"
    # (a file of another size there: written again)
    (card / "data" / "haloce-vita" / "maps" / "a10.map").write_bytes(b"short")
    once_more = hcv.copy_to_card(str(out), str(card), log=lambda line: None)
    assert once_more["copied"] == 1
    replaced = hcv.copy_to_card(str(out), str(card), replace=True, log=lambda line: None)
    assert replaced["skipped"] == 0 and replaced["copied"] == len(hcv.staged_uploads(str(out)))


def test_not_enough_space_writes_nothing(tmp_path, monkeypatch):
    out = staged_out(tmp_path)
    card = card_root(tmp_path / "card")
    real = hcv.shutil.disk_usage

    def tiny(path):
        usage = real(path)
        return type(usage)(usage.total, usage.used, 100)
    monkeypatch.setattr(hcv.shutil, "disk_usage", tiny)
    with pytest.raises(hcv.InstallerError, match="Not enough free space"):
        hcv.copy_to_card(str(out), str(card), log=lambda line: None)
    assert not (card / "data" / "haloce-vita").exists()


def test_a_failed_write_leaves_no_partial_file(tmp_path, monkeypatch):
    out = staged_out(tmp_path)
    card = card_root(tmp_path / "card")

    def broken(source, destination, offset, size, progress, label, done=0, total=0):
        destination.write(b"half")
        raise OSError("the card was unplugged")
    monkeypatch.setattr(hcv, "_copy_range", broken)
    with pytest.raises(OSError):
        hcv.copy_to_card(str(out), str(card), log=lambda line: None)
    assert not list(card.rglob("*.part"))


def test_folders_refused(tmp_path, monkeypatch):
    out = staged_out(tmp_path)
    mcc = tmp_path / "steam" / "Halo MCC"
    mcc.mkdir(parents=True)
    refused = [(str(out / "inside"), "inside the Halo files"),
               (str(mcc / "halo1"), "inside the Halo files"),
               ("", "Choose the memory card"),
               ("relative/folder", "Choose the memory card")]
    if sys.platform == "win32":
        refused += [(os.environ.get("SystemDrive", "C:") + "\\", "not the system drive"),
                    (os.path.join(os.environ["SystemRoot"], "Temp"), "system folder")]
    else:
        refused += [("/", "not the system drive"), ("/usr/share/halo", "system folder"), ("/etc", "system folder")]
    for bad, why in refused:
        with pytest.raises(hcv.InstallerError, match=why):
            hcv.check_card_folder(bad, [str(out), str(mcc)])
    # (the output folder inside the game folder that would be written)
    holder = tmp_path / "holder"
    inner = holder / "data" / "haloce-vita" / "gathered"
    inner.mkdir(parents=True)
    with pytest.raises(hcv.InstallerError, match="inside the Halo files"):
        hcv.check_card_folder(str(holder), [str(inner)])
    a_file = tmp_path / "a file"
    a_file.write_bytes(b"x")
    with pytest.raises(hcv.InstallerError, match="is a file"):
        hcv.check_card_folder(str(a_file))
    # (Windows' own folders, as a Windows Setup sees them)
    monkeypatch.setattr(hcv.sys, "platform", "win32")
    system = tmp_path / "Windows"
    monkeypatch.setenv("SystemRoot", str(system))
    for name in ("windir", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "ProgramData", "CommonProgramFiles",
                 "CommonProgramFiles(x86)"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("SystemDrive", str(tmp_path / "C:"))
    with pytest.raises(hcv.InstallerError, match="system folder"):
        hcv.check_card_folder(str(system / "Temp"))
    assert hcv.check_card_folder(str(tmp_path / "E")) == str(tmp_path / "E")
    monkeypatch.undo()
    # (a card's own folder, and a new folder, are fine)
    card = card_root(tmp_path / "card")
    assert hcv.check_card_folder(str(card), [str(out)]) == str(card)
    assert hcv.check_card_folder(str(card / "data" / "haloce-vita"), [str(out)]) == str(card)
    assert hcv.check_card_folder(str(tmp_path / "new" / "folder"), [str(out)]) == str(tmp_path / "new" / "folder")


def test_copy_to_card_refuses_its_own_output_folder(tmp_path):
    out = staged_out(tmp_path)
    with pytest.raises(hcv.InstallerError, match="inside the Halo files"):
        hcv.copy_to_card(str(out), str(out), log=lambda line: None)
    with pytest.raises(hcv.InstallerError, match="Nothing to copy"):
        hcv.copy_to_card(str(tmp_path / "empty"), str(tmp_path / "card"), log=lambda line: None)
    bad = tmp_path / "bad.vpk"
    bad.write_bytes(b"not a zip")
    with pytest.raises(hcv.InstallerError, match="not a VPK"):
        hcv.copy_to_card(str(out), str(tmp_path / "card"), str(bad), log=lambda line: None)


def test_command_line_card(tmp_path, capsys, monkeypatch):
    monkeypatch.setattr(hcv, "match_signed_vpk", lambda path, log=print: False)
    out = staged_out(tmp_path)
    card = card_root(tmp_path / "card")
    vpk = make_vpk(tmp_path / "halo.vpk")
    assert hcv.main(["--out", str(out), "card", str(card), "--vpk", str(vpk)]) == 0
    said = capsys.readouterr().out
    assert "a Vita memory card" in said and "press Cross on halo.vpk" in said
    assert (card / "data" / "haloce-vita" / "maps" / "a10.map").is_file() and (card / "halo.vpk").is_file()
    system = os.environ["SystemRoot"] if sys.platform == "win32" else "/etc"
    assert hcv.main(["--out", str(out), "card", system]) == 1
    assert "system folder" in capsys.readouterr().out


# -- the wizard


def test_destination_offers_usb_or_a_folder(root, tmp_path, monkeypatch):
    errors = complaints(monkeypatch)
    window = wizard(root, tmp_path)
    window.platform.set("vita")
    window.show("destination")
    root.update()
    assert "Test connection" in buttons(window)
    window.vita_via.set("folder")
    window.show("destination")
    root.update()
    words = " ".join(texts(window))
    assert "Test connection" not in buttons(window) and "USB mode" in words
    assert "Memory card's drive, or a folder:" in words
    window.next_button.invoke()
    assert window.page == "destination" and "Choose the memory card" in errors[-1]
    window.card_folder.set(str(tmp_path / "out" / "inside"))
    assert "inside the Halo files" in window.card_note.get()
    window.next_button.invoke()
    assert "inside the Halo files" in errors[-1]
    card = card_root(tmp_path / "card")
    window.card_folder.set(str(card))
    assert window.card_note.get().startswith("A Vita memory card") and "Space free" in window.card_note.get()
    window.card_folder.set(str(tmp_path / "somewhere"))
    assert window.card_note.get().startswith("Setup writes data/haloce-vita")
    window.download_vpk_box.set(False)
    advance(window, root, "ready")
    summary = window.summary_box.get("1.0", "end")
    assert "written to a folder" in summary and os.path.join("data", "haloce-vita") in summary


def test_vita_to_a_card_end_to_end(root, tmp_path, monkeypatch):
    """Gathered from MCC, the signed VPK downloaded, all written to the card
    over USB; Finish says what is left, and nothing goes over FTP."""
    errors = complaints(monkeypatch)
    monkeypatch.setattr(hcv, "upload_files", lambda *a, **k: pytest.fail("FTP used"))
    vpk = make_vpk(tmp_path / "halo-ce-vita-1.1.3-vita.vpk")

    def download(folder, progress=None, log=print):
        path = os.path.join(folder, vpk.name)
        with open(str(vpk), "rb") as source, open(path, "wb") as copy:
            copy.write(source.read())
        return path
    monkeypatch.setattr(hcv, "download_vpk", download)
    fake_steam(tmp_path, monkeypatch)
    card = card_root(tmp_path / "card")
    window = wizard(root, tmp_path)
    to_game_files(window, root, "vita")
    advance(window, root, "destination")
    window.vita_via.set("folder")
    window.show("destination")
    window.card_folder.set(str(card))
    advance(window, root, "ready")
    assert "its memory card over USB" in window.summary_box.get("1.0", "end")
    window.next_button.invoke()
    wait_for_finish(window, root)
    assert not errors and window.outcome == "done", window.reason
    game = card / "data" / "haloce-vita"
    assert (game / "mcc" / "halo1" / "maps" / "a10.map").is_file() and (card / vpk.name).is_file()
    assert (card / hcv.CARD_README).is_file()
    words = " ".join(texts(window))
    assert "on the Vita's memory card" in words and "press Cross on %s" % vpk.name in words
    assert "leave USB mode" in words and "uma0:" in words
    # (the last page's steps fit the window)
    root.update()
    bottom = window.shell.winfo_rooty() + window.shell.winfo_height()
    for widget in all_widgets(window.body):
        if widget.winfo_ismapped():
            assert widget.winfo_rooty() + widget.winfo_height() <= bottom, widget.cget("text")
