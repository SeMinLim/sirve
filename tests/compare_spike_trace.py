#!/usr/bin/env python3

import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class TraceEvent:
	pc: int
	raw: int
	reg_idx: Optional[int] = None
	reg_value: Optional[int] = None
	load_addr: Optional[int] = None
	store_addr: Optional[int] = None
	store_size: Optional[int] = None
	store_value: Optional[int] = None


def parseHex(value: str) -> int:
	return int(value, 16)


def parseSirveTrace(filename: str) -> list[TraceEvent]:
	events = []
	for line in Path(filename).read_text().splitlines():
		if not line.startswith("TRACE "):
			continue
		fields = {}
		for token in line.split()[1:]:
			if "=" not in token:
				continue
			key, value = token.split("=", 1)
			fields[key] = value
		if fields.get("status") != "ok":
			continue

		reg_idx = None
		reg_value = None
		if "rd" in fields:
			reg_idx = int(fields["rd"][1:])
			reg_value = parseHex(fields["value"])

		store_addr = None
		store_size = None
		store_value = None
		if "store" in fields:
			store_addr = parseHex(fields["store"])
			store_size = int(fields["size"])
			store_value = parseHex(fields["value"])
			store_value &= (1 << (store_size * 8)) - 1

		events.append(TraceEvent(
			pc=parseHex(fields["pc"]),
			raw=parseHex(fields["raw"]),
			reg_idx=reg_idx,
			reg_value=reg_value,
			load_addr=parseHex(fields["load"]) if "load" in fields else None,
			store_addr=store_addr,
			store_size=store_size,
			store_value=store_value,
		))
	return events


def parseSpikeTrace(filename: str) -> list[TraceEvent]:
	events = []
	linePattern = re.compile(
		r"^core\s+0:\s+\d+\s+(0x[0-9a-fA-F]+)\s+"
		r"\((0x[0-9a-fA-F]+)\)(.*)$"
	)
	regPattern = re.compile(r"\sx(\d+)\s+(0x[0-9a-fA-F]+)")
	memPattern = re.compile(
		r"\smem\s+(0x[0-9a-fA-F]+)(?:\s+(0x[0-9a-fA-F]+))?"
	)

	for line in Path(filename).read_text().splitlines():
		match = linePattern.match(line)
		if match is None:
			continue
		pcText, rawText, effects = match.groups()

		regMatch = regPattern.search(effects)
		regIdx = None
		regValue = None
		if regMatch is not None:
			regIdx = int(regMatch.group(1))
			regValue = parseHex(regMatch.group(2))

		loadAddr = None
		storeAddr = None
		storeSize = None
		storeValue = None
		for memMatch in memPattern.finditer(effects):
			addrText, valueText = memMatch.groups()
			if valueText is None:
				loadAddr = parseHex(addrText)
			else:
				storeAddr = parseHex(addrText)
				hexDigits = len(valueText) - 2
				storeSize = (hexDigits + 1) // 2
				storeValue = parseHex(valueText)

		events.append(TraceEvent(
			pc=parseHex(pcText),
			raw=parseHex(rawText),
			reg_idx=regIdx,
			reg_value=regValue,
			load_addr=loadAddr,
			store_addr=storeAddr,
			store_size=storeSize,
			store_value=storeValue,
		))
	return events


def formatEvent(event: TraceEvent) -> str:
	text = f"pc=0x{event.pc:08x} raw=0x{event.raw:08x}"
	if event.reg_idx is not None:
		text += f" rd=x{event.reg_idx:02d} value=0x{event.reg_value:08x}"
	if event.load_addr is not None:
		text += f" load=0x{event.load_addr:08x}"
	if event.store_addr is not None:
		text += (
			f" store=0x{event.store_addr:08x} size={event.store_size}"
			f" value=0x{event.store_value:08x}"
		)
	return text


def main() -> int:
	if len(sys.argv) != 3:
		print(f"usage: {sys.argv[0]} <sirve-trace> <spike-trace>")
		return 2

	sirveEvents = parseSirveTrace(sys.argv[1])
	spikeEvents = parseSpikeTrace(sys.argv[2])
	if not sirveEvents:
		print("SIRVE trace contains no retired instructions.")
		return 1
	if not spikeEvents:
		print("Spike trace contains no retired instructions.")
		return 1

	if len(sirveEvents) != len(spikeEvents):
		print(
			f"Trace length mismatch: SIRVE={len(sirveEvents)} "
			f"Spike={len(spikeEvents)}"
		)
		return 1

	for index, (sirveEvent, spikeEvent) in enumerate(zip(sirveEvents, spikeEvents)):
		if sirveEvent != spikeEvent:
			print(f"Trace mismatch at retired instruction {index + 1}:")
			print(f"SIRVE: {formatEvent(sirveEvent)}")
			print(f"Spike: {formatEvent(spikeEvent)}")
			return 1

	print(f"Matched {len(sirveEvents)} retired instructions.")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
