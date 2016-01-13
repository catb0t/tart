# A Taste of Tart #

Tart's goal is simple: Provide the power, flexibility and performance of a language like C++, with the simplicity, brevity, and sheer fun of languages like Python and Ruby. Part of Tart's speed comes from the fact that it is a _statically-typed_ language, and thus can avoid the need to do expensive run-time type checks. Tart's simplicity derives from its use of modern language features such as type inference and garbage collection, which removes much of the drudgery and typing from programming.

This overview is not meant to be a comprehensive guide to Tart, but is more in the nature of a brief tour of some of the language's highlights.

Although Tart is not yet complete, all of the techniques demonstrated here are supported by the current compiler.

Tart is an object-oriented language, and programmers who are familiar with Java, C# or Python will find much that is familiar, as in the following example:

```
/** Represents a contiguous range of characters. */
class CharacterRange {
  private {
    var first:char;
    var last:char;
  }

  def construct(first:char, last:char) {
    self.first = first;
    self.last = last;
  }

  // Return true if 'ch' is within this range.
  def contains(ch:char) -> bool {
    return ch >= first and ch < last;
  }
}
```

One difference that you may notice right away is that when declaring variables and function parameters, the data type always comes after the variable name, unlike C++ where the type and the name are often mixed together (as in `void main(int argc, char *argv[]);`). Type expressions in Tart are always read from left to right – first the base type, and then any type modifiers.

Some other features of the previous example that are worth noting:

  * The constructor of a class is always called `construct`.
  * The `self` variable refers to the current object.
  * Class members are always public unless declared otherwise.
  * Class methods are declared with the `def` keyword, variables are declared with `var`. There is also `let` which defines an immutable variable (similar to Java's 'final').
  * Instead of having to declare each individual member 'private', you can declare them in a private block.
  * The `->` symbol means “returns type”

Now that we've created a class, let's see what it takes to construct an instance of that class:

```
let lowerCaseChars = CharacterRange('a', 'z');
```

To create a new instance of a class, you can simply call the class like a function – same as in Python. Also, you may notice that we didn't declare the type of the variable `lowerCaseChars`, the compiler determined it automatically.

## Simple Widget Example ##

Now, let's look at a more sophisticated example, which will demonstrate the use of functions as first-class objects:

```
// A simple widget class example
class ScrollBar : Widget {
  private let minVal:int;
  private let maxVal:int;
  private let listeners:List[fn (:int)] = ArrayList();

  def construct(minVal:int, maxVal:int) {
    self.minVal = minVal;
    self.maxVal = maxVal;
  }

  // Add a new event listener.
  def addValueChangeListener(listener:fn (:int)) {
    listeners.add(listener);
  }
}

// Construct a new scrollbar
let sb = ScrollBar(minVal=0, maxVal=100);

// Add a change handler
sb.addValueChangeListener(
  fn (value:int) {
    Console.cout.writeLn(“New scrollbar value: {0}”.format(value));
  });
```

There's a lot going on here, but a few points are worth mentioning: The `listeners` list is an example of a generic `List` type – Tart uses square brackets to indicate type parameters. Note that we didn't need to tell the compiler what type of `ArrayList` to create – it figured out the type parameters from the type of the variable was being assigned to. This shows that the type inference system is bi-directional – you can infer the variable type from the initializer, and vice versa. You can also specify incompletely typed expressions (such as `ArrayList` with no type parameters) and it will attempt to fill in the missing information.

In this case, the type parameter is a function type which takes a single integer parameter. So the listeners list is really a list of function references. The `addValueChangeListener()` method takes a function reference and adds it to the listener list.

Later we construct a scrollbar, in this case using keyword arguments.

Finally at the bottom we create a listener function and add it to the scrollbar's listener list. In this case, it's an anonymous function – much like a lambda. The keyword `fn` can be used both to declare a variable of function type, as well as an actual function value.

## Enumeration Example ##

The next code example will demonstrate some of the more advanced features of Tart's type system. In this example, we will build the `enumerate()` function. This function wraps an iterator and adds sequence numbers to its output. In other words, if our original iterator produces the sequence "A, B, C", then `enumerate()` will return an iterator that produces the sequence "(0, A), (1, B), (2, C)";

The built-in `Iterator` interface in Tart is pretty simple, containing a single method named `next`:

```
interface Iterator[%T] {
  def next -> T or void;
}
```

The `Iterator` class is a template with a single type parameter, `T`. Type parameters are declared in square brackets immediately after the class or function name. The percent sign that appears in the declaration of `%T` tells the compiler that `T` is a _type variable_, rather than an actual type. Type variables can be bound to actual types either explicitly, or implicitly via deduction.

The return type of the `next()` method is a _union type_. A union can hold values of more than one type, although only one type at a time. In this case, the return type is `T or void`, meaning that `next()` either returns a `T` or nothing at all (the latter occurring when there are no more values in the sequence.) The typical usage pattern for an iterator is to keep calling `next()` until it stops returning values.

The `enumerate()` function itself is also fairly simple, as it delegates most of the work to the helper class `Enumerator`.

```
def enumerate[%T](iter:Iterator[T]) -> Iterator[(int, T)] {
  return Enumerator(iter);
}
```

As you can see, the `enumerate()` takes an `Iterator` as input, and returns an `Iterator` as its result. But the iterator types are different - the return type is an  iterator over _tuples_. A tuple in Tart is data structure which contains a sequence of values which may be different types - much like a struct except that the member fields have no names, only numbers. Tuples can be used to return multiple values from a function like so:

```
def sumAndDifference(a:int, b:int) -> (int, int) {
  return a + b, a - b;
}

let x, y = sumAndDifference(5, 10);
```

Now let's get back to the enumeration example and take a look at the `Enumerator` class:

```
class Enumerator[%T] : Iterator[(int, T)] {
  let iter:Iterator[T];
  var index:int = 0;
  
  def construct(iter:Iterator[T]) {
    self.iter = iter;
  }
  
  def next -> (int, T) or void {
    classify iter.next() {
      as value:T {
        return index++, value;
      } else {
        return;
      }
    }
  }
}
```

`Enumerator` implements the `Iterator` interface. Within the `next()` method, there is a _classify_ statement. Classify is similar to a switch statement, except that the case values are types - that is, which block gets executed depends on the type of the input.

In this case, we're using classify to determine what kind of data is stored in the union. The value we are attempting to classify is `iter.next()` - the next value in the iteration sequence, which has a type of `T or void`. If there are still values in the sequence, then the `iter.next()` will return a value of type `T`, in which case we return a tuple containing the index and the value. Otherwise, if `iter` has been exhausted, then it will return nothing, in which case we also return nothing.

To use the enumerator, we can use Tart's "for ... in" syntax.

```
for index, value in enumerate(["One", "Two", "Three"]) {
  Console.out.writeLn("{0}: {1}".format(index, value));
}
```

Note that "for ... in" works with any type that implements the `Iterator` interface.

(And in case you were wondering, the `next()` method of `Enumerator` comes out to roughly 50 machine instructions on Intel x86 with the current compiler.)