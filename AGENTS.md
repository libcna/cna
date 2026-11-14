# AI Contribution Policy (v4)

AI-assisted contributions are permitted under the conditions below.
The human contributor remains responsible for the entire contribution,
including any material generated or modified by AI.

“AI” includes Large Language Models (“LLMs”) and other generative AI tools,
such as ChatGPT, Claude, Copilot, and similar systems.

This policy applies to all project content, including source code, tests,
build scripts, configuration, documentation, examples, assets, commit
messages, pull or merge request descriptions, and issue reports.

## Authorization to make changes

AI must act only as an advisor unless a human explicitly authorizes it
to make changes for a defined task or scope.

In its advisory role, AI may analyze existing content, explain code and
architecture, research external documentation, identify possible issues,
and propose solutions or draft text for human consideration. It must not
apply changes to the project.

A human instruction to implement or fix something within a defined scope
authorizes the edits reasonably necessary for that task. Separate approval
is not required for every individual edit within that scope.

A request for advice, review, explanation, or a proposal does not authorize
changes. If the authorization is unclear, AI must ask before making changes.
If additional work falls outside the authorized scope, AI must obtain
further authorization.

Authorization to edit does not by itself authorize pushing, publishing,
or merging changes.

## Human understanding and responsibility

The human contributor must review the complete contribution, including
all AI-generated or AI-modified content.

Before submitting the contribution, the human contributor must:

- Understand the changes and their effects.
- Be able to explain their purpose, behavior, and relevant design choices.
- Verify factual claims and confirm that reported issues actually exist.
- Perform appropriate validation and inspect the actual results.
- Correct or remove errors, unnecessary changes, and other deficiencies.
- Be able to address review feedback and correct problems in the contribution.

AI-generated explanations, another person's approval, or passing automated
checks do not replace the contributor's own understanding.

AI-assisted changes must not be submitted or merged if the human contributor
does not understand them. If the contributor cannot adequately evaluate or
explain a change, they must obtain the necessary understanding or remove
that change before proceeding.

These responsibilities apply equally to contributions made without AI.

## Git commits

AI must not create Git commits, amend commits, rewrite commit history,
or sign commits. This prohibition applies whether performed directly
or indirectly through tools, scripts, APIs, or other automation.

Operations that create or rewrite commits must be performed by a human.

AI may propose commit messages for human review. The human must verify
that the final message accurately describes the changes and must perform
the commit operation themselves.

Permission to modify project content does not waive this restriction.

## Quality, provenance, and licensing

AI output must be treated as unverified material.

The human contributor must evaluate the contribution for correctness,
maintainability, security, and compliance with project requirements.

Using AI does not establish the origin, ownership, or licensing status
of its output. The contributor must ensure that they have the necessary
rights to contribute all included material and preserve any required
attribution and notices.

Contributors must not describe AI-generated material as entirely
human-authored.

## Disclosure and submission

Pull and merge requests must state whether AI was used to generate or
modify contribution content. If it was, briefly describe its role,
the affected parts, and the validation performed.

By submitting a contribution, the human contributor confirms that they
understand and accept responsibility for it, have the necessary rights
to contribute it under the project's applicable license terms, and have
complied with this policy.

## Project-specific instructions

Additional project-specific instructions may be provided in `AI.md`.
That file is optional.

Those instructions supplement this policy. They do not by themselves
authorize edits or waive the requirements for human understanding,
human responsibility, or human-performed commits.
