# Development history

This clean source snapshot begins from the reviewed `0.1.132` implementation.
It intentionally does not import the earlier repository history or the large
collection of phase reports, DAW checklists, recordings, logs, crash material,
and machine-specific test artifacts created during private development.

Those records remain in the private historical archive. They are evidence for
past engineering decisions, not current product requirements. Current scope,
architecture, build instructions, migration evidence, and future work are
maintained in the canonical documents in this tree.

Useful verified conclusions from the final private baseline are summarized in
`docs/VALIDATION.md`; private environment details were not copied.

Active development in the independent repository begins at version `0.2.0`.
Version `0.2.1` deliberately replaces the temporary private Classic and
Visage-spike host identities with one permanent CLAP ID and explicit VST3 CID.
Because those development builds were not publicly released, the project
accepts this one-time DAW resolution break before establishing its public
identity. Parameter identities and V1--V20 saved-state decoding remain intact.
