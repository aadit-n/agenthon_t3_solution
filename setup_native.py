"""Compile hot modules to C at image build time; never JIT in the timed run."""
from setuptools import Extension, setup
from Cython.Build import cythonize

MODULES = [
    "abides_core.agent", "abides_core.kernel",
    "abides_markets.orders", "abides_markets.price_level", "abides_markets.order_book",
    "abides_markets.agents.trading_agent", "abides_markets.agents.exchange_agent",
    "abides_fork.agents", "t3sim.hot", "t3sim.engine", "t3sim.level",
]
setup(
    name="agenthon-t3-native",
    ext_modules=cythonize(
        [Extension(name, [name.replace(".", "/") + ".py"], extra_compile_args=["-O3"])
         for name in MODULES],
        compiler_directives={"language_level": 3, "annotation_typing": False,
                             "binding": True, "infer_types": False},
        nthreads=4,
    ),
)
