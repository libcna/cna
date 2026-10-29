# SPDX-License-Identifier: MS-PL
"""Fixture definitions, one module per owning group.

Every asset has exactly one canonical id and is defined in exactly one of these modules -- its
**owning group**. Other groups may reference it (recorded as ``referencingGroups``), which never
re-counts it.
"""
