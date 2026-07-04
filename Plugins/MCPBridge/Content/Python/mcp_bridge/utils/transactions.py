"""Transaction wrappers for UE4 undo support."""

import functools
from typing import Any, Callable, Dict


def transactional(description: str) -> Callable:
    """Decorator that wraps a handler function in a UE4 transaction.
    
    Usage:
        @transactional("Spawn Actor")
        def handle_actor_spawn(params):
            ...
    
    The transaction enables Ctrl+Z undo in the editor for the operation.
    If the handler raises an exception, the transaction is still closed
    to prevent undo stack corruption.
    """
    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(params: Dict[str, Any]) -> Dict[str, Any]:
            try:
                import unreal
                unreal.SystemLibrary.begin_transaction("MCP Bridge", description, None)
            except ImportError:
                pass
            
            try:
                result = func(params)
            except Exception as e:
                try:
                    import unreal
                    unreal.SystemLibrary.end_transaction()
                except ImportError:
                    pass
                raise e
            
            try:
                import unreal
                unreal.SystemLibrary.end_transaction()
            except ImportError:
                pass
            
            return result
        
        # Store reference to unwrapped function for batch operations
        wrapper.__wrapped__ = func
        return wrapper
    
    return decorator
