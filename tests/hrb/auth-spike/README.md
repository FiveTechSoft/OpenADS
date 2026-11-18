# TEST ONLY: HRB challenge auth spike probes

`test_only.prg` deliberately has NO cryptography. It accepts challenge bytes
followed by `test-only` and returns fixed synthetic DB user `usr`, password
`pw`. Never deploy it. Compile to HRB with your Harbour compiler, protect the
HRB ACL (Linux0600), and point serverd --auth_module at it. This checks bridge,
state and existing DB-login routing, not the owner's credential scheme.

worker_probe takes absolute worker executable and HRB paths.
abi_probe takes auth://host:port/path. Start dedicated serverd with --data
pointing at the path and --auth_user usr:pw. Compile probes WITHOUT NDEBUG
because their assertions perform test operations. Link ABI probe to the
matching ACE or core build. Timeout test expects the supervisor to kill an
infinite loop; next probe must still work. Replay fails and briefly throttles
the source IP. ABI probe allows1.2s before its next request.
