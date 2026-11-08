"""
Generic payload contract inspectors for schema formats (XSD, JSON Schema, etc.).
Allows any handler to expose payload shapes to the DSL editor without hardcoding handler names.
"""

from __future__ import annotations
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET


def _parse_xsd_element(elem: ET.Element) -> dict[str, Any]:
    """
    Recursively parses an xs:element into a field descriptor tree, detecting
    types, arrays (maxOccurs > 1 or 'unbounded'), and child complexType sequences.
    """
    name = elem.attrib.get("name", "element")
    elem_type = elem.attrib.get("type", "string")
    if ":" in elem_type:
        elem_type = elem_type.split(":")[-1]

    max_occurs = elem.attrib.get("maxOccurs", "1")
    is_array = (max_occurs == "unbounded") or (max_occurs.isdigit() and int(max_occurs) > 1)

    children: list[dict[str, Any]] = []

    # Look for complexType inside this element
    for child in elem:
        tag_local = child.tag.split("}")[-1] if "}" in child.tag else child.tag
        if tag_local == "complexType":
            elem_type = "object"
            for sub in child.iter():
                sub_tag = sub.tag.split("}")[-1] if "}" in sub.tag else sub.tag
                if sub_tag == "element" and sub is not elem and sub.attrib.get("name"):
                    children.append(_parse_xsd_element(sub))

    res: dict[str, Any] = {
        "name": name,
        "type": "array" if is_array else elem_type,
        "isArray": is_array,
        "itemType": elem_type if is_array else None,
        "doc": f"XML element <{name}>" + (" (array/list)" if is_array else f" ({elem_type})"),
    }
    if children:
        res["children"] = children
        if is_array:
            res["itemType"] = "object"

    return res


def inspect_xsd(file_path: Path | str) -> dict[str, Any]:
    """
    Parses an XSD schema file into a hierarchical payload definition tree.
    """
    p = Path(file_path)
    if not p.is_file():
        return {"error": f"XSD file not found: {p}"}

    try:
        tree = ET.parse(p)
        root = tree.getroot()
        elements: list[dict[str, Any]] = []

        for child in root:
            tag_local = child.tag.split("}")[-1] if "}" in child.tag else child.tag
            if tag_local == "element":
                elements.append(_parse_xsd_element(child))

        return {
            "format": "xml",
            "rootElements": elements,
        }
    except Exception as e:
        return {"error": f"Failed to parse XSD: {e}"}


PAYLOAD_INSPECTORS = {
    "xsd": inspect_xsd,
    "xml": inspect_xsd,
}


def inspect_payload_contract(contract_format: str, file_path: Path | str) -> dict[str, Any]:
    """
    Generic dispatcher querying the format-specific inspector.
    """
    inspector = PAYLOAD_INSPECTORS.get(contract_format.lower())
    if not inspector:
        return {"error": f"Unsupported payload contract format: {contract_format}"}
    return inspector(file_path)
