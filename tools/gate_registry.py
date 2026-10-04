"""Required campaign contracts. Missing implementations cannot produce a pass."""
GATES = {
    'fresh-build': {'command': ['make', 'build'], 'minimum': 4, 'unit': 'strict-host-profile'},
    'original-pipeline': {'command': ['make', 'test-native-linux'], 'minimum': 1, 'unit': 'original-native-pipeline'},
    'feature-families': {'command': ['make', 'test-frontend'], 'minimum': 48, 'unit': 'source-linked-family'},
    'authored-suite': {'command': ['make', 'test'], 'minimum': 1200, 'unit': 'authored-source'},
    'differential-programs': {'command': ['make', 'test-differential'], 'minimum': 20000, 'unit': 'eligible-native-differential'},
    'ir-semantics': {'command': ['make', 'test-ir-campaign'], 'minimum': 100000, 'unit': 'typed-ir'},
    'rewrite-validation': {'command': ['make', 'test-rewrites'], 'minimum': 50000, 'unit': 'independent-rewrite'},
    'nonvacuous-passes': {'command': ['make', 'test-passes'], 'minimum': 10, 'unit': 'positive-negative-pass-pair'},
    'abi': {'command': ['make', 'test-abi'], 'minimum': 500, 'unit': 'native-abi-combination'},
    'allocation': {'command': ['make', 'test-allocation'], 'minimum': 10000, 'unit': 'independent-pressure-allocation'},
    'object-output': {'command': ['make', 'test-object-campaign'], 'minimum': 1000, 'unit': 'native-object-probe'},
    'debugging': {'command': ['make', 'test-debug'], 'minimum': 25, 'unit': 'debugger-session'},
    'native-fuzzing': {'command': ['make', 'fuzz'], 'minimum': 4000000, 'unit': 'coverage-guided-execution'},
    'failure-recovery': {'command': ['make', 'test-recovery'], 'minimum': 1000, 'unit': 'owned-resource-recovery'},
    'applications': {'command': ['make', 'test-apps'], 'minimum': 8, 'unit': 'nontrivial-application'},
    'self-hosting': {'command': ['make', 'selfhost'], 'minimum': 1000, 'unit': 'stage-two-and-three-native-program'},
    'performance': {'command': ['make', 'benchmark'], 'minimum': 1, 'unit': 'frozen-native-performance-campaign'},
    'documentation-product': {'command': ['make', 'demo'], 'minimum': 1, 'unit': 'verified-cli-explorer-documentation'},
    'publication': {'command': ['git', 'ls-remote', 'origin', 'refs/heads/main'], 'minimum': 1, 'unit': 'hosted-tested-tip'},
    'private-size': {'command': ['python3', 'tools/source_census.py', '--output', '.agent-local/source-census.json'], 'minimum': 10000, 'unit': 'substantive-production-line'},
}

# Connect dedicated readers only after their complete campaign artifacts exist.
# A threshold and a self-reported status are insufficient evidence.
AUDITED_REPORT_READERS = frozenset()
