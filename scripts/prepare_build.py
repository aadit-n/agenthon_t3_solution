"""Remove disabled diagnostic formatting before ahead-of-time compilation.

Only logging.debug/info expression statements are removed; market event logging,
RNG calls and all simulation branches remain. Vendored originals stay intact.
"""
import ast
import pathlib
import sys


class TrimDiagnostics(ast.NodeTransformer):
    def visit_Expr(self, node):
        call = node.value
        if (isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
                and isinstance(call.func.value, ast.Name)
                and call.func.value.id == "logger"
                and call.func.attr in {"debug", "info"}):
            return ast.copy_location(ast.Pass(), node)
        return self.generic_visit(node)


def prepare(root):
    for package in ("abides_core", "abides_markets"):
        for path in (root / package).rglob("*.py"):
            tree = TrimDiagnostics().visit(ast.parse(path.read_text(encoding="utf8")))
            if path == root / "abides_markets/utils/__init__.py":
                spatial = [node for node in tree.body if isinstance(node, ast.ImportFrom)
                           and node.module == "scipy.spatial.distance"]
                tree.body = [node for node in tree.body if node not in spatial]
                for node in tree.body:
                    if isinstance(node, ast.FunctionDef) and node.name == "generate_uniform_random_pairwise_dist_on_line":
                        index = int(bool(node.body and isinstance(node.body[0], ast.Expr)
                                         and isinstance(node.body[0].value, ast.Constant)
                                         and isinstance(node.body[0].value.value, str)))
                        node.body[index:index] = spatial
            path.write_text(ast.unparse(ast.fix_missing_locations(tree)) + "\n", encoding="utf8")
    # All sparse-oracle methods used by the scenario adapter are overridden;
    # emit those exact methods without its unused dense/pandas/kernel imports.
    source=root / 'abides_markets/oracles/sparse_mean_reverting_oracle.py'
    tree=ast.parse(source.read_text(encoding='utf8'))
    tree.body=[node for node in tree.body if not
               (isinstance(node,ast.ImportFrom) and node.module in
                ('abides_core','mean_reverting_oracle')) and not
               (isinstance(node,ast.Import) and any(alias.name=='pandas' for alias in node.names))]
    for node in tree.body:
        if isinstance(node,ast.ClassDef):
            node.bases=[]
    tree.body.insert(0,ast.ImportFrom(module='__future__',names=[ast.alias(name='annotations')],level=0))
    tree.body.append(ast.Assign(targets=[ast.Name(id='NanosecondTime',ctx=ast.Store())],value=ast.Name(id='int',ctx=ast.Load())))
    (root / 't3sim/oracle.py').write_text(ast.unparse(ast.fix_missing_locations(tree))+'\n',encoding='utf8')


if __name__ == "__main__":
    prepare(pathlib.Path(sys.argv[1]))
