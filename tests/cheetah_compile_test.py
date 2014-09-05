from __future__ import absolute_import
from __future__ import unicode_literals

import io
import os.path
import pytest

from Cheetah.cheetah_compile import _compile_files_in_directory
from Cheetah.cheetah_compile import _touch_init_if_not_exists
from Cheetah.cheetah_compile import compile_directories
from Cheetah.cheetah_compile import compile_template
from Cheetah.cheetah_compile import compile_all
from testing.util import run_python


# pylint:disable=redefined-outer-name


@pytest.yield_fixture
def template_writer(tmpdir):
    class TemplateWriter(object):
        def __init__(self):
            self.tmpl_number = 0

        def write(self, src):
            self.tmpl_number += 1
            tmpl_path = os.path.join(
                tmpdir.strpath, 'a{0}.tmpl'.format(self.tmpl_number)
            )
            with io.open(tmpl_path, 'w') as tmpl_file:
                tmpl_file.write(src)

            return tmpl_path
    yield TemplateWriter()


def _test_compile_template(tmpl_file):
    tmpl_py = compile_template(tmpl_file)
    assert os.path.exists(tmpl_py)
    ret = run_python(tmpl_py)
    assert ret == 'Hello world'


def test_compile_template(template_writer):
    tmpl_file = template_writer.write('Hello world')
    _test_compile_template(tmpl_file)


def test_compile_template_with_bytes(template_writer):
    """argv passes bytes in py2"""
    tmpl_file = template_writer.write('Hello world')
    filename = tmpl_file.encode('UTF-8')
    assert isinstance(filename, bytes)
    _test_compile_template(filename)


def test_compile_multiple_files(template_writer):
    tmpl1 = template_writer.write('foo')
    tmpl2 = template_writer.write('bar')
    compile_all([tmpl1, tmpl2])
    assert run_python(tmpl1.replace('.tmpl', '.py')) == 'foo'
    assert run_python(tmpl2.replace('.tmpl', '.py')) == 'bar'


def test_compile_multiple_files_by_directory(template_writer, tmpdir):
    tmpl1 = template_writer.write('foo')
    tmpl2 = template_writer.write('bar')
    compile_all([tmpdir.strpath])
    assert run_python(tmpl1.replace('.tmpl', '.py')) == 'foo'
    assert run_python(tmpl2.replace('.tmpl', '.py')) == 'bar'


def test_touch_init_if_not_exists(tmpdir):
    _touch_init_if_not_exists(tmpdir.strpath)
    assert os.path.exists(os.path.join(tmpdir.strpath, '__init__.py'))


def test_touch_init_if_not_exists_already_exists(tmpdir):
    init_py_path = os.path.join(tmpdir.strpath, '__init__.py')
    with io.open(init_py_path, 'w') as init_py:
        init_py.write('print("Hello World")\n')

    _touch_init_if_not_exists(tmpdir.strpath)
    assert io.open(init_py_path).read() == 'print("Hello World")\n'


def test_compile_files_in_directory_no_files(tmpdir):
    assert not _compile_files_in_directory(tmpdir.strpath, ())


def test_compile_files_in_directory_some_files(tmpdir):
    with io.open(os.path.join(tmpdir.strpath, 'foo.tmpl'), 'w') as foo_tmpl:
        foo_tmpl.write('Hello world')

    assert _compile_files_in_directory(tmpdir.strpath, ('foo.tmpl',))
    assert os.path.exists(os.path.join(tmpdir.strpath, 'foo.py'))


def test_compile_files_not_right_extension(tmpdir):
    with io.open(os.path.join(tmpdir.strpath, 'foo.tmpl'), 'w') as foo_tmpl:
        foo_tmpl.write('Hello world')

    assert not _compile_files_in_directory(
        tmpdir.strpath,
        ('foo.tmpl',),
        extension='.txt',
    )
    assert not os.path.exists(os.path.join(tmpdir.strpath, 'foo.py'))


def test_compile_file_with_other_extension(tmpdir):
    with io.open(os.path.join(tmpdir.strpath, 'foo.txt'), 'w') as foo_tmpl:
        foo_tmpl.write('Hello world')

    with io.open(os.path.join(tmpdir.strpath, 'bar.tmpl'), 'w') as bar_tmpl:
        bar_tmpl.write('Hi world')

    assert _compile_files_in_directory(
        tmpdir.strpath,
        ('foo.txt', 'bar.tmpl'),
        extension='.txt',
    )

    assert os.path.exists(os.path.join(tmpdir.strpath, 'foo.py'))
    assert not os.path.exists(os.path.join(tmpdir.strpath, 'bar.py'))


def test_compile_directories_no_init_in_non_template_dirs(tmpdir):
    non_templates = os.path.join(tmpdir.strpath, 'no_templates')
    os.mkdir(non_templates)
    templates = os.path.join(tmpdir.strpath, 'templates')
    os.mkdir(templates)
    with io.open(os.path.join(templates, 'foo.tmpl'), 'w') as foo_tmpl:
        foo_tmpl.write('Hello world')

    compile_directories([tmpdir.strpath])
    assert os.path.exists(os.path.join(templates, '__init__.py'))
    assert os.path.exists(os.path.join(templates, 'foo.py'))
